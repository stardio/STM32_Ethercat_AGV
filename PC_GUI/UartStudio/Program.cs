namespace UartStudio;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) => ShowFatal(e.Exception);
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is Exception ex)
            {
                ShowFatal(ex);
            }
        };

        try
        {
            ApplicationConfiguration.Initialize();
            Application.Run(new MainForm());
        }
        catch (Exception ex)
        {
            ShowFatal(ex);
        }
    }

    private static void ShowFatal(Exception ex)
    {
        var text = $"UartStudio crashed at startup.\n\n{ex}";
        try
        {
            var logPath = Path.Combine(AppContext.BaseDirectory, "fatal.log");
            File.WriteAllText(logPath, text);
            MessageBox.Show(text + $"\n\nLog: {logPath}", "UartStudio Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        catch
        {
            MessageBox.Show(text, "UartStudio Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
