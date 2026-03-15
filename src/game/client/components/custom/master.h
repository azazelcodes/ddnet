#pragma once

#include <game/client/component.h>
#include <engine/client.h>
#include <engine/shared/http.h>

class CCMaster : public CComponent
{
	std::shared_ptr<CHttpRequest> m_pRTokenTask = nullptr;
    char m_RToken[128] = {0};

	std::shared_ptr<CHttpRequest> m_pRPlayersTask = nullptr;
public:
    int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
    void OnReset() override;
    void OnJoin();
    void OnMessage(int type, void *pRaw) override;
    int NumPlayers = 0;

    void HandleNewSnapshot(const IClient::CSnapItem *pItem);

    int InsertCustomClientIdSC(int col);
    int TryGetClient(int ClientId);
    void FetchRushieToken();
    void FinishRushieToken();
    void FetchRushiePlayers(const char *pServerAddress);
    void FinishRushiePlayers();
    void ApplyRHeaders(CHttpRequest &Request, const char *pServerAddress);
    struct SRClientUserInfo
	{
		std::string m_ServerAddress;
		int m_PlayerId;
	};
    std::vector<SRClientUserInfo> m_vRushiePlayers;
};