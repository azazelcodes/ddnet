#include "master.h"

#include <engine/client.h>
#include <engine/external/ddnet-custom-clients/custom_clients_ids.h>
#include <engine/external/json-parser/json.h>

#include <game/client/gameclient.h>

void CCMaster::OnInit()
{
	FetchRushieToken();
}
void CCMaster::OnRender()
{
	if(m_RToken[0] == '\0')
		FinishRushieToken();
	FinishRushiePlayers();

	const bool Teams = GameClient()->IsTeamPlay();
	const auto &aTeamSize = GameClient()->m_Snap.m_aTeamSize;
	const int nNumPlayers = Teams ? maximum(aTeamSize[TEAM_RED], aTeamSize[TEAM_BLUE]) : aTeamSize[TEAM_RED];
	if(nNumPlayers > NumPlayers) OnJoin();
	NumPlayers = nNumPlayers;
}
void CCMaster::OnReset()
{
	GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Custom", "RESET");
}
void CCMaster::OnJoin()
{
	CServerInfo m_CurrentServerInfo;
	Client()->GetServerInfo(&m_CurrentServerInfo);
	const char *pServerAddress = m_CurrentServerInfo.m_aAddress;
	FetchRushiePlayers(pServerAddress);
}

void CCMaster::OnMessage(int type, void *pRaw)
{
	char aBuf[128];
	
	if(type == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRaw;
		str_format(aBuf, sizeof(aBuf), "%d k %d via %d (%d)", pMsg->m_Killer, pMsg->m_Victim, pMsg->m_Weapon, pMsg->m_ModeSpecial);
	}
	else str_format(aBuf, sizeof(aBuf), "%d", type);

	GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Custom", aBuf);
}



int CCMaster::TryGetClient(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return -1;
	
	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	for(const auto &User : m_vRushiePlayers)
	{	
		if(str_comp(User.m_ServerAddress.c_str(), CurrentServerInfo.m_aAddress) == 0 && User.m_PlayerId == ClientId) 
			return CUSTOM_CLIENT_ID_CHILLERBOTUX+1; // above country flags, unreachable otherwise => Rushie Client
	}

	return GameClient()->m_aClients[ClientId].m_CustomClient;
}



// function originally from Kaizo Client by +KZ, credit if used
int CCMaster::InsertCustomClientIdSC(int col)
{
	union
	{
		int c = 0;
		unsigned char b[4];
	} data;
	data.c = col;
	data.b[3] = (unsigned char)CCID_COLOR_BODY_CUSTOM;
	return data.c;
}

void CCMaster::HandleNewSnapshot(const IClient::CSnapItem *pItem)
{
	const CNetObj_ClientInfo *pInfo = (const CNetObj_ClientInfo *)pItem->m_pData;
	int ClientId = pItem->m_Id;
	if(ClientId < MAX_CLIENTS && ClientId >= 0)
	{
		union
		{
			int c = 0;
			unsigned char b[4];
		} data;

		data.c = pInfo->m_ColorBody;

		if(data.b[3] == 0)
			return;

		GameClient()->m_aClients[ClientId].m_CustomClient =
			data.b[3] == CCID_COLOR_BODY_KAIZO_CLIENT ? CUSTOM_CLIENT_ID_KAIZO_NETWORK :
			data.b[3] == CCID_COLOR_BODY_PDUCKCLIENT ? CUSTOM_CLIENT_ID_PDUCKCLIENT :
			data.b[3] == CCID_COLOR_BODY_CUSTOM ? CUSTOM_CLIENT_ID_CUSTOM :
			data.b[3] == CCID_COLOR_BODY_CHILLERBOTUX ? CUSTOM_CLIENT_ID_CHILLERBOTUX :
			-1;
	}
}

/*
 * Modified from RushieClient-ddnet
 * src/game/client/components/rclient/rclient_indicator.cpp
 */
void CCMaster::FetchRushieToken()
{
	if(m_pRTokenTask && !m_pRTokenTask->Done())
		return;
	m_pRTokenTask = HttpGet("https://server.rushie-client.ru/token");
	m_pRTokenTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pRTokenTask);
}
void CCMaster::FinishRushieToken()
{
	if(m_pRTokenTask->State() != EHttpState::DONE)
		return;

	json_value *pJson = m_pRTokenTask->ResultJson();
	if(!pJson)
		return;

	const json_value &Json = *pJson;
	const json_value &Token = Json["token"];
	if(Token.type != json_string)
		return;

	str_copy(m_RToken, Token.u.string.ptr, sizeof(m_RToken));
	str_utf8_trim_right(m_RToken);
	json_value_free(pJson);

	
	GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Custom", m_RToken);
}

void CCMaster::FetchRushiePlayers(const char *pServerAddress)
{
	if(m_pRPlayersTask && !m_pRPlayersTask->Done())
		return;
	m_pRPlayersTask = HttpGet("https://server.rushie-client.ru/users.json");
	ApplyRHeaders(*m_pRPlayersTask, pServerAddress);
	m_pRPlayersTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pRPlayersTask);
}
void CCMaster::FinishRushiePlayers() // FIXME: move to pClient->m_CustomClient
{
	if(!m_pRPlayersTask)
		return;
	if(m_pRPlayersTask->State() != EHttpState::DONE)
		return;
	if(!m_vRushiePlayers.empty()) // clear before each run?
		return;

	const int Status = m_pRPlayersTask->StatusCode();
	if(Status == 401 || Status == 403)
	{
		m_RToken[0] = '\0';
		FinishRushieToken();
		return;
	}

	json_value *pJson = m_pRPlayersTask->ResultJson();
	if(!pJson)
		return;

	const json_value &Json = *pJson;
	const json_value &Error = Json["error"];
	if(Error.type == json_string)
	{
		json_value_free(pJson);
		return;
	}

	if(Json.type != json_object)
	{
		json_value_free(pJson);
		return;
	}

	for(unsigned int i = 0; i < Json.u.object.length; i++)
	{
		const char *pServerAddr = Json.u.object.values[i].name;
		const json_value &PlayersObj = *Json.u.object.values[i].value;

		if(pServerAddr[0] == '_')
			continue;

		if(PlayersObj.type != json_object)
			continue;

		for(unsigned int j = 0; j < PlayersObj.u.object.length; j++)
		{
			const char *pPlayerIdStr = PlayersObj.u.object.values[j].name;
			int PlayerId = atoi(pPlayerIdStr);
			const json_value &PlayerData = *PlayersObj.u.object.values[j].value;
			m_vRushiePlayers.push_back({std::string(pServerAddr), PlayerId});
		}
	}

	json_value_free(pJson);
}
void CCMaster::ApplyRHeaders(CHttpRequest &Request, const char *pServerAddress)
{
	Request.HeaderString("X-RClient-Token", m_RToken);
	Request.HeaderString("X-RClient-Server", pServerAddress);
	Request.HeaderInt("X-RClient-Since", 0);
	Request.HeaderInt("X-RClient-Timeout", 20);
	Request.HeaderInt("X-RClient-Voice", false);

	
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%s", pServerAddress);
	GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Custom", aBuf);
}