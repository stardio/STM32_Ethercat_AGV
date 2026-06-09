using UartWeb.Models;

namespace UartWeb.Services;

public static class AnomalyDetector
{
    public sealed record Result(string Type, double Score);

    private const double PeakHighRatio = 1.30;   /* 기준 피크 대비 130% 초과 */
    private const double PeakLowRatio  = 0.60;   /* 기준 피크 대비 60% 미달 */
    private const double RmsThreshold  = 15.0;   /* 기준 피크 대비 RMS 편차 15% */

    public static Result Analyze(BaselineProfile baseline, GraphSamplePoint[] points)
    {
        if (points.Length < 2) return new("ok", 0.0);

        var bl = baseline;
        if (bl.TorqueAvg.Length == 0) return new("ok", 0.0);

        /* abs(pos), abs(torque), sorted by pos */
        var cyc = points
            .Select(p => (pos: Math.Abs((double)p.Pos), tq: Math.Abs((double)p.Torque)))
            .OrderBy(p => p.pos)
            .ToArray();

        double cyclePeak   = cyc.Max(p => p.tq);
        double baselinePeak = bl.TorqueAvg.Max();
        if (baselinePeak < 1.0) baselinePeak = 1.0;

        /* 피크 비율 검사 */
        double peakRatio = cyclePeak / baselinePeak;
        if (peakRatio > PeakHighRatio)
            return new("peak_high", Math.Round((peakRatio - 1.0) * 100.0, 1));
        if (peakRatio < PeakLowRatio)
            return new("peak_low", Math.Round((1.0 - peakRatio) * 100.0, 1));

        /* 프로파일 RMS 편차 검사 */
        double sumSq = 0; int cnt = 0;
        for (int gi = 0; gi < bl.TorqueAvg.Length; gi++)
        {
            double g    = bl.PosMin + gi * bl.Resolution;
            double blTq = bl.TorqueAvg[gi];
            double cyTq = Interp(cyc, g);
            if (!double.IsNaN(cyTq)) { sumSq += (cyTq - blTq) * (cyTq - blTq); cnt++; }
        }

        double rmsNorm = cnt > 0 ? Math.Sqrt(sumSq / cnt) / baselinePeak * 100.0 : 0.0;
        return rmsNorm > RmsThreshold
            ? new("profile", Math.Round(rmsNorm, 1))
            : new("ok",      Math.Round(rmsNorm, 1));
    }

    public static double[] DeviationPerGrid(BaselineProfile bl, GraphSamplePoint[] points)
    {
        var cyc = points
            .Select(p => (pos: Math.Abs((double)p.Pos), tq: Math.Abs((double)p.Torque)))
            .OrderBy(p => p.pos)
            .ToArray();

        var dev = new double[bl.TorqueAvg.Length];
        for (int i = 0; i < bl.TorqueAvg.Length; i++)
        {
            double g    = bl.PosMin + i * bl.Resolution;
            double cyTq = Interp(cyc, g);
            dev[i] = double.IsNaN(cyTq) ? 0.0 : cyTq - bl.TorqueAvg[i];
        }
        return dev;
    }

    private static double Interp((double pos, double tq)[] pts, double g)
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
}
