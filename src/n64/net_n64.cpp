// The game's network session (NetPlay.cpp, DirectPlay) and lobby dialogs
// (Network_20324.cpp) are not built: the N64 has no network. This file provides
// the calls the game makes, with the answers it expects from single player.
#include <string.h>

#include "NetPlay.hpp"
#include "Network_20324.hpp"

DEFINE_GLOBAL(NetPlay, gNetPlay_7071E8, 0x7071E8);

NetPlay::NetPlay()
{
    field_8F0_disabled = 1;
}

NetPlay::~NetPlay()
{
}

s32 NetPlay::SetProtoAndConnection_51DAE0(GUID*, Connection_Unknown*)
{
    return 0;
}

s32 NetPlay::InitializeConnection_51E5C0()
{
    return 0;
}

void NetPlay::Disconnect_520D10()
{
    field_8F0_disabled = 1;
}

s32 NetPlay::DeletePlayerFromGroup_521000(u32)
{
    return 0;
}

void NetPlay::GetPlayerName_521100(wchar_t* Destination, u32)
{
    Destination[0] = 0;
}

void NetPlay::SetExitGameCallBack_521330(s32, Game_0x40*)
{
}

s32 NetPlay::GetMaxPlayers_521350()
{
    return 1;
}

bool NetPlay::WaitForPlayersSync_5213E0()
{
    return false;
}

s32 NetPlay::SendToPlayer_521630(Network_8*, s32, char_type)
{
    return 0;
}

char_type NetPlay::ReceiveGameMessage_521890(Network_8*, s32*, u32*)
{
    return 0;
}

void NetPlay::SendToAll_521B20(Network_8*)
{
}

s32 NetPlay::SendKeepAlive_521D20()
{
    return 0;
}

s32 NetPlay::SendGiveUp_521DB0(s32)
{
    return 0;
}

static char_type gNoMap[1] = {0};

Network_20324::Network_20324()
{
}

Network_20324::~Network_20324()
{
}

s32 Network_20324::ShowNetworkUiBlocking_519BD0(HINSTANCE)
{
    return 0;
}

void Network_20324::CopyGameSettings_51C7F0(NetworkGameSettings* pSettings)
{
    memset(pSettings, 0, sizeof(*pSettings));
    pSettings->field_20194_frag_limit = 0x7FFFFFFF;
    pSettings->field_20198_game_type = 2; // POINTS_GAME_2
    pSettings->field_201A0_police_on = 1;
}

char_type* Network_20324::GetMapName_51CA10()
{
    return gNoMap;
}

char_type* Network_20324::GetMapStyName_51CA50()
{
    return gNoMap;
}

char_type* Network_20324::GetMapScrName_51CA90()
{
    return gNoMap;
}
