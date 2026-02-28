void uade_arch_kill_and_wait_uadecore(struct uade_ipc *ipc, pid_t *uadepid)
{
    if (*uadepid == 0)
        return;

    if (uade_send_short_message(UADE_COMMAND_QUIT, ipc)) {
        uade_warning("Could not send poison pill to uadecore (%d)\n", *uadepid);
    }

    uade_atomic_close(ipc->in_fd);
    uade_atomic_close(ipc->out_fd);

    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, *uadepid);
    if (handle) {
        TerminateProcess(handle, 0);
        CloseHandle(handle);
    }

    *uadepid = 0;
    WSACleanup();
}

int uade_arch_spawn(struct uade_ipc *ipc, pid_t *uadepid, const char *uadename, const int */*keep_fds*/)
{
    WSADATA wsaData;
    SOCKET sockets[2];
    char cmdline[PATH_MAX];

    if (WSAStartup(MAKEWORD(2,2), &wsaData)) {
        int error = WSAGetLastError();
        uade_warning("Could not init winsock: %s (%d)\n", strerror(error), error);
        return -1;
    }

    if (dumb_socketpair(sockets, 0)) {
        int error = WSAGetLastError();
        uade_warning("Could not create socketpair: %s (%d)\n", strerror(error), error);
        WSACleanup();
        return -1;
    }

    // give in/out fds as command line parameters to uadecore
    // NOTE: windows subprocess cannot inherit the fd, pass the socket handle instead
    snprintf(cmdline, sizeof cmdline, "\"%s\" -i %d -o %d", uadename, sockets[1], sockets[1]);

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if( !CreateProcess(
        NULL,           // No module name (use command line)
        cmdline,        // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        TRUE,           // Set handle inheritance to TRUE
        CREATE_NO_WINDOW, // Do not open console window
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi )           // Pointer to PROCESS_INFORMATION structure
    ) {
        int error = GetLastError();
       	uade_warning("CreateProcess (%s) failed: %s (%d)\n", uadename, strerror(error), error);
        closesocket(sockets[0]);
        closesocket(sockets[1]);
        WSACleanup();
        return -1;
    }

    *uadepid = pi.dwProcessId;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    closesocket(sockets[1]);

    int fd = _open_osfhandle(sockets[0], O_RDWR|O_BINARY);
    uade_set_peer(ipc, 1, fd, fd);
    return 0;
}
