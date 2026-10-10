# Two copies of the game, and LAN (dev_tools)

## Starting the game twice
- `main()` 0x666590 begins with `IsTheGameAlreadyRunning("speed.exe", 1)` (0x6CBC00, cdecl): a Toolhelp process snapshot, counts processes by exe name, retries up to 3 times with `Sleep(1000)`.
  The ASI loader loads plugins about when `main` runs (~2 s after process start), so hooking the function is too late for the first count; `dev_tools` instead refuses
  `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` (the function reads `INVALID_HANDLE_VALUE` as "not running" on its next count, ~1 s later) [tested].
- GameTest (widescreen fix `WindowedMode = 4`): both copies run. Vanilla 1.3: the second copy dies at 0x6E6D5F (null Direct3D device: no windowed mode without the widescreen fix).
- The profile directory is shared (`Documents\NFS Most Wanted\<profile>`): use two profile names. `Logger::open_in_data_folder` falls back to `name.2.log` ... `name.9.log` if another copy holds the file.

## LAN
- UDP broadcast on port 9999 (socket with `SO_BROADCAST`, no `SO_REUSEADDR`), 384-byte packets to 255.255.255.255:9999, header `67 45 41 <type>` (0 search, 3 announcement "NFSM...", 0x1e other);
  receives with overlapped `WSARecvFrom` (and gets its own broadcast back). TCP: the host binds `<LAN ip>:9900` and clients connect; a raw ICMP socket pings the host first. Network code is
  DirtySock-style at 0x84AB40..0x866BB0 (receive thread `sub_84AB40`).
- **Two PCs work** (Windows + Linux laptop under Proton with `WINEDLLOVERRIDES="dinput8=n,b"`) [tested]. Two copies on **one** PC do not: a second socket cannot bind 9999 without `SO_REUSEADDR`,
  and with it the copies hear each other's announcements but the searching copy does not list the host (no ping, no connect); rewriting the source address to 127.0.0.2 did not help. Unsolved; the
  workaround code was removed. `dev_tools` keeps a Winsock log (`[network] log`).
