#pragma once

// Local preservation host: tells the local backend (127.0.0.1 only) that the game world is listening,
// so matchmaking assigns the client only once the server can actually accept it.

#include <windows.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace BattleLab
{
	inline bool PostToBackend(const std::wstring& Path)
	{
		bool bOk = false;
		HINTERNET Session = WinHttpOpen(L"BattleLabHost/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!Session)
			return false;

		if (HINTERNET Connect = WinHttpConnect(Session, L"127.0.0.1", 3551, 0))
		{
			if (HINTERNET Request = WinHttpOpenRequest(Connect, L"POST", Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0))
			{
				if (WinHttpSendRequest(Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(Request, nullptr))
				{
					DWORD Status = 0, Size = sizeof(Status);
					WinHttpQueryHeaders(Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &Size, WINHTTP_NO_HEADER_INDEX);
					bOk = Status >= 200 && Status < 300;
				}
				WinHttpCloseHandle(Request);
			}
			WinHttpCloseHandle(Connect);
		}
		WinHttpCloseHandle(Session);
		return bOk;
	}

	inline std::string GetFromBackend(const std::wstring& Path)
	{
		std::string Body;
		HINTERNET Session = WinHttpOpen(L"BattleLabHost/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!Session)
			return Body;

		if (HINTERNET Connect = WinHttpConnect(Session, L"127.0.0.1", 3551, 0))
		{
			if (HINTERNET Request = WinHttpOpenRequest(Connect, L"GET", Path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0))
			{
				if (WinHttpSendRequest(Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(Request, nullptr))
				{
					DWORD Available = 0;
					while (WinHttpQueryDataAvailable(Request, &Available) && Available)
					{
						std::string Chunk(Available, '\0');
						DWORD Read = 0;
						if (!WinHttpReadData(Request, Chunk.data(), Available, &Read) || !Read)
							break;
						Body.append(Chunk.data(), Read);
					}
				}
				WinHttpCloseHandle(Request);
			}
			WinHttpCloseHandle(Connect);
		}
		WinHttpCloseHandle(Session);
		return Body;
	}

	// Multiplayer settings from the local backend's LocalServerConfig.json (GET /battlelab/hostconfig, "key=value" lines):
	// lan=1 when the player hosts for friends (LAN, Hamachi or another VPN), cheats=everyone|host. Read once.
	struct FHostConfig
	{
		bool bLan = false;
		bool bCheatsForEveryone = false;
	};

	inline const FHostConfig& GetHostConfig()
	{
		static FHostConfig Config = []
		{
			FHostConfig Result;
			std::string Text = GetFromBackend(L"/battlelab/hostconfig");
			Result.bLan = Text.find("lan=1") != std::string::npos;
			Result.bCheatsForEveryone = Text.find("cheats=everyone") != std::string::npos;
			return Result;
		}();
		return Config;
	}

	inline void NotifyHostReady(int Port, const std::string& Playlist)
	{
		std::wstring Path = L"/battlelab/host/ready?pid=" + std::to_wstring(GetCurrentProcessId()) + L"&port=" + std::to_wstring(Port) + L"&playlist=";
		Path += std::wstring(Playlist.begin(), Playlist.end());
		// Retry briefly in case the backend is busy; never blocks the game thread for long.
		for (int Attempt = 0; Attempt < 3; ++Attempt)
		{
			if (PostToBackend(Path))
				return;
			Sleep(200);
		}
	}
}
