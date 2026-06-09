using System.Globalization;
using System.Text;
using System.Text.Json;
using Microsoft.Data.Sqlite;
using UartWeb.Models;

namespace UartWeb.Services;

public sealed class GraphDb : IDisposable
{
    private readonly SqliteConnection _conn;
    private readonly object _dbLock = new();

    public GraphDb()
    {
        var dbPath = Path.Combine(AppContext.BaseDirectory, "cycle_graph.db");
        _conn = new SqliteConnection($"Data Source={dbPath}");
        _conn.Open();
        EnsureSchema();
    }

    private void EnsureSchema()
    {
        using var cmd = _conn.CreateCommand();
        cmd.CommandText = """
            PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS cycles (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                cycle_no     INTEGER NOT NULL,
                timestamp    TEXT    NOT NULL,
                result       TEXT    NOT NULL,
                peak_force   INTEGER NOT NULL,
                end_pos      INTEGER NOT NULL,
                cycle_ms     INTEGER NOT NULL,
                point_count  INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS graph_points (
                id       INTEGER PRIMARY KEY AUTOINCREMENT,
                cycle_id INTEGER NOT NULL REFERENCES cycles(id),
                seq      INTEGER NOT NULL,
                pos      INTEGER NOT NULL,
                torque   INTEGER NOT NULL,
                step     INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_gp_cycle ON graph_points(cycle_id, seq);
            """;
        cmd.ExecuteNonQuery();

        /* Schema migration: add new columns if they don't exist yet */
        MigrateAddColumn("cycles",       "velocity_peak",  "INTEGER DEFAULT 0");
        MigrateAddColumn("cycles",       "torque_area",    "INTEGER DEFAULT 0");
        MigrateAddColumn("graph_points", "velocity",       "INTEGER DEFAULT 0");
        MigrateAddColumn("graph_points", "ms",             "INTEGER DEFAULT 0");
        MigrateAddColumn("cycles",       "anomaly_type",   "TEXT DEFAULT 'none'");
        MigrateAddColumn("cycles",       "anomaly_score",  "REAL DEFAULT 0");

        /* Baseline (Golden Signature) table */
        using var bl = _conn.CreateCommand();
        bl.CommandText = """
            CREATE TABLE IF NOT EXISTS baseline (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                created_at   TEXT    NOT NULL,
                sample_count INTEGER NOT NULL,
                pos_min      INTEGER NOT NULL,
                pos_max      INTEGER NOT NULL,
                resolution   INTEGER NOT NULL,
                torque_json  TEXT    NOT NULL,
                std_json     TEXT    NOT NULL,
                active       INTEGER NOT NULL DEFAULT 1
            );
            """;
        bl.ExecuteNonQuery();
    }

    private void MigrateAddColumn(string table, string column, string definition)
    {
        try
        {
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = $"ALTER TABLE {table} ADD COLUMN {column} {definition}";
            cmd.ExecuteNonQuery();
        }
        catch { /* column already exists — ignore */ }
    }

    public long SaveCycle(CycleResultSnapshot result, GraphSamplePoint[] points)
    {
        /* Derived metrics for trend analysis */
        int velocityPeak = points.Length > 0 ? points.Max(p => Math.Abs(p.Velocity)) : 0;
        long torqueArea  = points.Sum(p => (long)Math.Abs(p.Torque)) * 2; /* Σ|tq| × 2ms */

        lock (_dbLock)
        {
            using var tx = _conn.BeginTransaction();
            long cycleId;

            using (var cmd = _conn.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = """
                    INSERT INTO cycles
                        (cycle_no, timestamp, result, peak_force, end_pos, cycle_ms, point_count,
                         velocity_peak, torque_area)
                    VALUES ($cn, $ts, $res, $pf, $ep, $cms, $pc, $vp, $ta);
                    SELECT last_insert_rowid();
                    """;
                cmd.Parameters.AddWithValue("$cn",  result.CycleNumber);
                cmd.Parameters.AddWithValue("$ts",  result.TimestampUtc.ToString("O", CultureInfo.InvariantCulture));
                cmd.Parameters.AddWithValue("$res", result.Result);
                cmd.Parameters.AddWithValue("$pf",  result.PeakForcePct);
                cmd.Parameters.AddWithValue("$ep",  result.EndPosition);
                cmd.Parameters.AddWithValue("$cms", result.CycleTimeMs);
                cmd.Parameters.AddWithValue("$pc",  points.Length);
                cmd.Parameters.AddWithValue("$vp",  velocityPeak);
                cmd.Parameters.AddWithValue("$ta",  torqueArea);
                cycleId = (long)(cmd.ExecuteScalar() ?? 0L);
            }

            using (var cmd = _conn.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = """
                    INSERT INTO graph_points (cycle_id, seq, pos, torque, step, velocity, ms)
                    VALUES ($cid, $seq, $pos, $tq, $st, $vel, $ms)
                    """;
                var pCid = cmd.Parameters.Add("$cid", SqliteType.Integer);
                var pSeq = cmd.Parameters.Add("$seq", SqliteType.Integer);
                var pPos = cmd.Parameters.Add("$pos", SqliteType.Integer);
                var pTq  = cmd.Parameters.Add("$tq",  SqliteType.Integer);
                var pSt  = cmd.Parameters.Add("$st",  SqliteType.Integer);
                var pVel = cmd.Parameters.Add("$vel", SqliteType.Integer);
                var pMs  = cmd.Parameters.Add("$ms",  SqliteType.Integer);

                pCid.Value = cycleId;
                for (int i = 0; i < points.Length; i++)
                {
                    pSeq.Value = i;
                    pPos.Value = points[i].Pos;
                    pTq.Value  = points[i].Torque;
                    pSt.Value  = points[i].Step;
                    pVel.Value = points[i].Velocity;
                    pMs.Value  = points[i].Ms;
                    cmd.ExecuteNonQuery();
                }
            }

            tx.Commit();
            return cycleId;
        }
    }

    public CycleDbListResponse ListCycles(int limit = 200, int offset = 0)
    {
        lock (_dbLock)
        {
            long total;
            using (var cmd = _conn.CreateCommand())
            {
                cmd.CommandText = "SELECT COUNT(*) FROM cycles";
                total = (long)(cmd.ExecuteScalar() ?? 0L);
            }

            var list = new List<CycleDbRecord>();
            using (var cmd = _conn.CreateCommand())
            {
                cmd.CommandText = """
                    SELECT id, cycle_no, timestamp, result, peak_force, end_pos, cycle_ms, point_count,
                           anomaly_type, anomaly_score, torque_area
                    FROM cycles ORDER BY id DESC LIMIT $lim OFFSET $off
                    """;
                cmd.Parameters.AddWithValue("$lim", limit);
                cmd.Parameters.AddWithValue("$off", offset);
                using var reader = cmd.ExecuteReader();
                while (reader.Read())
                {
                    list.Add(ReadCycleRecord(reader));
                }
            }

            return new CycleDbListResponse { Total = total, Records = [.. list] };
        }
    }

    public CycleDbRecord? GetCycle(long id)
    {
        lock (_dbLock)
        {
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = """
                SELECT id, cycle_no, timestamp, result, peak_force, end_pos, cycle_ms, point_count,
                       anomaly_type, anomaly_score, torque_area
                FROM cycles WHERE id = $id
                """;
            cmd.Parameters.AddWithValue("$id", id);
            using var reader = cmd.ExecuteReader();
            return reader.Read() ? ReadCycleRecord(reader) : null;
        }
    }

    public GraphSamplePoint[] GetPoints(long cycleId)
    {
        lock (_dbLock)
        {
            var list = new List<GraphSamplePoint>();
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = """
                SELECT pos, torque, step, velocity, ms FROM graph_points
                WHERE cycle_id = $cid ORDER BY seq
                """;
            cmd.Parameters.AddWithValue("$cid", cycleId);
            using var reader = cmd.ExecuteReader();
            while (reader.Read())
            {
                list.Add(new GraphSamplePoint
                {
                    Pos      = reader.GetInt32(0),
                    Torque   = reader.GetInt32(1),
                    Step     = reader.GetInt32(2),
                    Velocity = reader.GetInt32(3),
                    Ms       = reader.GetInt32(4),
                });
            }
            return [.. list];
        }
    }

    public bool DeleteCycle(long id)
    {
        lock (_dbLock)
        {
            using var tx = _conn.BeginTransaction();
            using (var cmd = _conn.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = "DELETE FROM graph_points WHERE cycle_id = $id";
                cmd.Parameters.AddWithValue("$id", id);
                cmd.ExecuteNonQuery();
            }
            int rows;
            using (var cmd = _conn.CreateCommand())
            {
                cmd.Transaction = tx;
                cmd.CommandText = "DELETE FROM cycles WHERE id = $id";
                cmd.Parameters.AddWithValue("$id", id);
                rows = cmd.ExecuteNonQuery();
            }
            tx.Commit();
            return rows > 0;
        }
    }

    public string ExportCycleCsv(long id)
    {
        var cycle = GetCycle(id);
        if (cycle is null) return string.Empty;
        var points = GetPoints(id);

        var sb = new StringBuilder();
        sb.AppendLine(string.Format(CultureInfo.InvariantCulture,
            "# cycle_no={0},result={1},timestamp={2},peak_force={3:F1}%,end_pos={4}mm,cycle_ms={5}",
            cycle.CycleNo, cycle.Result, cycle.Timestamp,
            cycle.PeakForce / 10.0, cycle.EndPos, cycle.CycleMs));
        sb.AppendLine("seq,ms,pos_mm,velocity,torque_01pct,torque_pct,step");
        for (int i = 0; i < points.Length; i++)
        {
            sb.AppendLine(string.Format(CultureInfo.InvariantCulture,
                "{0},{1},{2},{3},{4},{5:F1},{6}",
                i, points[i].Ms, points[i].Pos, points[i].Velocity,
                points[i].Torque, points[i].Torque / 10.0, points[i].Step));
        }
        return sb.ToString();
    }

    public string ExportAllSummaryCsv()
    {
        var resp = ListCycles(limit: int.MaxValue, offset: 0);
        var sb = new StringBuilder();
        sb.AppendLine("id,cycle_no,timestamp,result,peak_force_pct,end_pos_mm,cycle_ms,point_count");
        foreach (var r in resp.Records)
        {
            sb.AppendLine(string.Format(CultureInfo.InvariantCulture,
                "{0},{1},{2},{3},{4:F1},{5},{6},{7}",
                r.Id, r.CycleNo, r.Timestamp, r.Result,
                r.PeakForce / 10.0, r.EndPos, r.CycleMs, r.PointCount));
        }
        return sb.ToString();
    }

    private static CycleDbRecord ReadCycleRecord(SqliteDataReader r)
    {
        var pointCount  = r.GetInt32(7);
        var torqueArea  = r.IsDBNull(10) ? 0L : r.GetInt64(10);
        /* torque_area = Σ|tq| × 2ms → average = torqueArea / (2 × pointCount) */
        var torqueAvg   = (pointCount > 0) ? (int)(torqueArea / (2 * pointCount)) : 0;
        return new CycleDbRecord
        {
            Id           = r.GetInt64(0),
            CycleNo      = r.GetInt64(1),
            Timestamp    = r.GetString(2),
            Result       = r.GetString(3),
            PeakForce    = r.GetInt32(4),
            EndPos       = r.GetInt64(5),
            CycleMs      = r.GetInt64(6),
            PointCount   = pointCount,
            TorqueAvg    = torqueAvg,
            AnomalyType  = r.IsDBNull(8) ? "none" : r.GetString(8),
            AnomalyScore = r.IsDBNull(9) ? 0.0    : r.GetDouble(9),
        };
    }

    public void UpdateCycleAnomaly(long id, string type, double score)
    {
        lock (_dbLock)
        {
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = """
                UPDATE cycles SET anomaly_type = $t, anomaly_score = $s WHERE id = $id
                """;
            cmd.Parameters.AddWithValue("$t",  type);
            cmd.Parameters.AddWithValue("$s",  score);
            cmd.Parameters.AddWithValue("$id", id);
            cmd.ExecuteNonQuery();
        }
    }

    public int ReanalyzeAll(BaselineProfile baseline)
    {
        List<long> ids;
        lock (_dbLock)
        {
            ids = new List<long>();
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = "SELECT id FROM cycles ORDER BY id";
            using var r = cmd.ExecuteReader();
            while (r.Read()) ids.Add(r.GetInt64(0));
        }
        int count = 0;
        foreach (var id in ids)
        {
            var pts = GetPoints(id);
            var res = AnomalyDetector.Analyze(baseline, pts);
            UpdateCycleAnomaly(id, res.Type, res.Score);
            count++;
        }
        return count;
    }

    /* ── Golden Signature Baseline ───────────────────────────────────── */

    public BaselineProfile? BuildBaseline(int cycleCount = 10)
    {
        lock (_dbLock)
        {
            /* Last N cycles by id */
            var cycleIds = new List<long>();
            using (var cmd = _conn.CreateCommand())
            {
                cmd.CommandText = "SELECT id FROM cycles ORDER BY id DESC LIMIT $n";
                cmd.Parameters.AddWithValue("$n", Math.Clamp(cycleCount, 3, 200));
                using var r = cmd.ExecuteReader();
                while (r.Read()) cycleIds.Add(r.GetInt64(0));
            }
            if (cycleIds.Count == 0) return null;

            /* Collect (absPos, absTorque) per cycle */
            var allSeries = new List<(double pos, double tq)[]>();
            foreach (var cid in cycleIds)
            {
                var pts = GetPoints(cid);
                if (pts.Length < 2) continue;
                var sorted = pts
                    .Select(p => (pos: Math.Abs((double)p.Pos),
                                  tq:  Math.Abs((double)p.Torque)))
                    .OrderBy(p => p.pos)
                    .ToArray();
                allSeries.Add(sorted);
            }
            if (allSeries.Count == 0) return null;

            /* Grid: 0..posMax in 1 mm steps */
            int posMax = (int)allSeries.Max(s => s.Max(p => p.pos));
            const int posMin    = 0;
            const int resolution = 1;
            int gridN = posMax - posMin + 1;

            /* Interpolate each cycle onto grid, then average */
            var buckets = new List<double>[gridN];
            for (int i = 0; i < gridN; i++) buckets[i] = [];

            foreach (var series in allSeries)
            {
                for (int gi = 0; gi < gridN; gi++)
                {
                    double g  = posMin + gi * resolution;
                    double tq = LinearInterp(series, g);
                    if (!double.IsNaN(tq)) buckets[gi].Add(tq);
                }
            }

            var avgArr = new double[gridN];
            var stdArr = new double[gridN];
            for (int i = 0; i < gridN; i++)
            {
                var v = buckets[i];
                if (v.Count == 0) continue;
                double avg = v.Average();
                avgArr[i] = avg;
                stdArr[i] = v.Count > 1
                    ? Math.Sqrt(v.Sum(x => (x - avg) * (x - avg)) / (v.Count - 1))
                    : 0.0;
            }

            /* Deactivate old baselines, insert new */
            using (var cmd = _conn.CreateCommand())
            {
                cmd.CommandText = "UPDATE baseline SET active = 0";
                cmd.ExecuteNonQuery();
            }

            var now        = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture);
            var torqueJson = JsonSerializer.Serialize(avgArr);
            var stdJson    = JsonSerializer.Serialize(stdArr);
            long newId;
            using (var cmd = _conn.CreateCommand())
            {
                cmd.CommandText = """
                    INSERT INTO baseline
                        (created_at, sample_count, pos_min, pos_max, resolution, torque_json, std_json, active)
                    VALUES ($ca, $sc, $pmin, $pmax, $res, $tq, $std, 1);
                    SELECT last_insert_rowid();
                    """;
                cmd.Parameters.AddWithValue("$ca",   now);
                cmd.Parameters.AddWithValue("$sc",   allSeries.Count);
                cmd.Parameters.AddWithValue("$pmin", posMin);
                cmd.Parameters.AddWithValue("$pmax", posMax);
                cmd.Parameters.AddWithValue("$res",  resolution);
                cmd.Parameters.AddWithValue("$tq",   torqueJson);
                cmd.Parameters.AddWithValue("$std",  stdJson);
                newId = (long)(cmd.ExecuteScalar() ?? 0L);
            }

            return new BaselineProfile
            {
                Id          = newId,
                CreatedAt   = now,
                SampleCount = allSeries.Count,
                PosMin      = posMin,
                PosMax      = posMax,
                Resolution  = resolution,
                TorqueAvg   = avgArr,
                TorqueStd   = stdArr,
            };
        }
    }

    public BaselineProfile? GetActiveBaseline()
    {
        lock (_dbLock)
        {
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = """
                SELECT id, created_at, sample_count, pos_min, pos_max, resolution, torque_json, std_json
                FROM baseline WHERE active = 1 ORDER BY id DESC LIMIT 1
                """;
            using var r = cmd.ExecuteReader();
            if (!r.Read()) return null;
            return ReadBaseline(r);
        }
    }

    public bool DeleteActiveBaseline()
    {
        lock (_dbLock)
        {
            using var cmd = _conn.CreateCommand();
            cmd.CommandText = "DELETE FROM baseline WHERE active = 1";
            return cmd.ExecuteNonQuery() > 0;
        }
    }

    private static BaselineProfile ReadBaseline(SqliteDataReader r) => new()
    {
        Id          = r.GetInt64(0),
        CreatedAt   = r.GetString(1),
        SampleCount = r.GetInt32(2),
        PosMin      = r.GetInt32(3),
        PosMax      = r.GetInt32(4),
        Resolution  = r.GetInt32(5),
        TorqueAvg   = JsonSerializer.Deserialize<double[]>(r.GetString(6)) ?? [],
        TorqueStd   = JsonSerializer.Deserialize<double[]>(r.GetString(7)) ?? [],
    };

    private static double LinearInterp((double pos, double tq)[] pts, double g)
    {
        if (pts.Length == 0) return double.NaN;
        if (g <= pts[0].pos)  return pts[0].tq;
        if (g >= pts[^1].pos) return pts[^1].tq;
        int lo = 0, hi = pts.Length - 1;
        while (lo < hi - 1)
        {
            int mid = (lo + hi) / 2;
            if (pts[mid].pos <= g) lo = mid; else hi = mid;
        }
        double t = (g - pts[lo].pos) / (pts[hi].pos - pts[lo].pos);
        return pts[lo].tq + t * (pts[hi].tq - pts[lo].tq);
    }

    public void Dispose()
    {
        lock (_dbLock) { _conn.Dispose(); }
    }
}
