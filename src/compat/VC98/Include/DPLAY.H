/* DirectPlay data types referenced by NetPlay.hpp (public layouts). The
 * DirectPlay transport itself is not built: the N64 has no network. */
#pragma once
#include <windows.h>
typedef DWORD DPID, *LPDPID;
typedef struct DPSESSIONDESC2
{
    DWORD dwSize, dwFlags;
    GUID guidInstance, guidApplication;
    DWORD dwMaxPlayers, dwCurrentPlayers;
    union { LPWSTR lpszSessionName; LPSTR lpszSessionNameA; };
    union { LPWSTR lpszPassword; LPSTR lpszPasswordA; };
    DWORD dwReserved1, dwReserved2, dwUser1, dwUser2, dwUser3, dwUser4;
} DPSESSIONDESC2, *LPDPSESSIONDESC2;
typedef struct DPNAME
{
    DWORD dwSize, dwFlags;
    union { LPWSTR lpszShortName; LPSTR lpszShortNameA; };
    union { LPWSTR lpszLongName; LPSTR lpszLongNameA; };
} DPNAME, *LPDPNAME;
typedef struct DPCOMPORTADDRESS { DWORD dwComPort, dwBaudRate, dwStopBits, dwParity, dwFlowControl; } DPCOMPORTADDRESS;
typedef struct DPMSG_ADDPLAYERTOGROUP { DWORD dwType; DPID dpIdGroup, dpIdPlayer; } DPMSG_ADDPLAYERTOGROUP;
struct IDirectPlay3;
struct IDirectPlayLobby2;
typedef IDirectPlay3 *LPDIRECTPLAY3A;
typedef IDirectPlayLobby2 *LPDIRECTPLAYLOBBY2A;
