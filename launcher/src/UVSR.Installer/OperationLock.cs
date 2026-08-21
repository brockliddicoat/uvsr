using System.Security.Principal;

namespace UvsrInstaller;

internal sealed class OperationLock : IDisposable
{
    private readonly ManualResetEventSlim _release = new(false);
    private readonly Thread _ownerThread;
    private Exception? _failure;

    private OperationLock(string name)
    {
        using ManualResetEventSlim ready = new(false);
        _ownerThread = new Thread(() => OwnMutex(name, ready))
        {
            IsBackground = true,
            Name = "UVSR installer operation lock"
        };
        try
        {
            _ownerThread.Start();
        }
        catch (Exception ex)
        {
            throw new InstallerException("Windows could not start UVSR Launcher safely.", ex);
        }
        ready.Wait();
        if (_failure is not null)
        {
            _release.Set();
            _ownerThread.Join();
            throw new InstallerException(
                "Another UVSR install, update, repair, or uninstall is already running.",
                _failure);
        }
    }

    internal static OperationLock Acquire()
    {
        string sid = WindowsIdentity.GetCurrent().User?.Value
            ?? throw new InstallerException("Windows could not identify the current user.");
        return new OperationLock(
            $@"Global\UVSR.Installer.{ProductConstants.ProductId}.{sid}");
    }

    private void OwnMutex(string name, ManualResetEventSlim ready)
    {
        Mutex? mutex = null;
        bool acquired = false;
        try
        {
            mutex = new Mutex(false, name);
            try { acquired = mutex.WaitOne(0); }
            catch (AbandonedMutexException) { acquired = true; }
            if (!acquired)
                _failure = new IOException("The named operation lock is held.");
            ready.Set();
            if (acquired)
                _release.Wait();
        }
        catch (Exception ex)
        {
            _failure = ex;
            ready.Set();
        }
        finally
        {
            if (acquired)
                mutex?.ReleaseMutex();
            mutex?.Dispose();
        }
    }

    public void Dispose()
    {
        _release.Set();
        _ownerThread.Join();
        _release.Dispose();
    }
}
