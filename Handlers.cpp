#include "Gamehooking.hpp"
#include "helpers/Utils.hpp"

#include "Menu.hpp"
#include "Options.hpp"

#include "helpers/Math.hpp"

#include "features/Visuals.hpp"
#include "features/Glow.hpp"
#include "features/Miscellaneous.hpp"
#include "features/PredictionSystem.hpp"
#include "features/AimRage.hpp"
#include "features/AimLegit.h"
#include "features/LagCompensation.hpp"
#include "features/Resolver.hpp"
#include "features/AntiAim.hpp"
#include "features/PlayerHurt.hpp"
#include "features/BulletImpact.hpp"
#include "features/GrenadePrediction.h"
#include "features/ServerSounds.hpp"
#include "features/Skinchanger.hpp"
#include "Install.hpp"
#include <intrin.h>
#include <experimental/filesystem> // hack

extern LRESULT ImGui_ImplDX9_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int32_t originalCorrectedFakewalkIdx = 0;
int32_t tickHitPlayer = 0;
int32_t tickHitWall = 0;
int32_t originalShotsMissed = 0;


namespace Global
{
	char my_documents_folder[MAX_PATH];

	float smt = 0.f;
	QAngle visualAngles = QAngle(0.f, 0.f, 0.f);
	bool bSendPacket = false;
	bool bAimbotting = false;
	bool bVisualAimbotting = false;
	QAngle vecVisualAimbotAngs = QAngle(0.f, 0.f, 0.f);
	CUserCmd *userCMD = nullptr;

	char *szLastFunction = "<No function was called>";
	HMODULE hmDll = nullptr;

	bool bFakelag = false;
	float flFakewalked = 0.f;
	Vector vecUnpredictedVel = Vector(0, 0, 0);

	float flFakeLatencyAmount = 0.f;
	float flEstFakeLatencyOnServer = 0.f;

	matrix3x4_t traceHitboxbones[128];
	INetChannel* netchan = nullptr;
	std::array<std::string, 64> resolverModes;

	int aimbot_target;
	int AimTargets = 0;
	Vector vecAimpos = Vector(0, 0, 0);
	bool bShouldUnload = false;
}

void debug_visuals()
{
	int x_offset = 0;
	if (g_Options.debug_showposes)
	{
		RECT bbox = Visuals::ESP_ctx.bbox;
		auto poses = Visuals::ESP_ctx.player->m_flPoseParameter();
		for (int i = 0; i < 24; ++i)
			Visuals::DrawString(Visuals::ui_font, bbox.right + 5, bbox.top + i * 12, Color(10 * i, 255, 255, 255), FONT_LEFT, "Pose %d %f", i, poses[i]);
		Visuals::DrawString(Visuals::ui_font, bbox.right + 5, bbox.top + - 12, Color(255, 0, 55, 255), FONT_LEFT, "LBY %f", (Visuals::ESP_ctx.player->m_flLowerBodyYawTarget() + 180.f) / 360.f);
		x_offset += 50;
	}

	if (g_Options.debug_showactivities)
	{
		float h = fabs(Visuals::ESP_ctx.feet_pos.y - Visuals::ESP_ctx.head_pos.y);
		float w = h / 2.0f;

		int offsety = 0;
		for (int i = 0; i < Visuals::ESP_ctx.player->GetNumAnimOverlays(); i++)
		{
			auto layer = Visuals::ESP_ctx.player->GetAnimOverlays()[i];
			int number = layer.m_nSequence,
				activity = Visuals::ESP_ctx.player->GetSequenceActivity(number);
			Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 4, Visuals::ESP_ctx.head_pos.y + offsety, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(number).c_str());
			Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 40, Visuals::ESP_ctx.head_pos.y + offsety, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(activity).c_str());
			Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 60, Visuals::ESP_ctx.head_pos.y + offsety, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(layer.m_flCycle).c_str());
			Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 104, Visuals::ESP_ctx.head_pos.y + offsety, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(layer.m_flWeight).c_str());
			Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 144, Visuals::ESP_ctx.head_pos.y + offsety, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(layer.m_flPlaybackRate).c_str());

			/*if (activity == 979)
			{
				Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 65, Visuals::ESP_ctx.head_pos.y + offsety, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(layer.m_flWeight).c_str());
				Visuals::DrawString(Visuals::ui_font, Visuals::ESP_ctx.head_pos.x + w + x_offset + 65, Visuals::ESP_ctx.head_pos.y + offsety + 12, Color(255, 255, 0, 255), FONT_LEFT, std::to_string(layer.m_flCycle).c_str());
			}*/

			offsety += 12;
		}
		x_offset += 100;
	}

	if (g_Options.debug_headbox)
	{
		Vector headpos = Visuals::ESP_ctx.player->GetBonePos(8);
		Visuals::Draw3DCube(7.f, Visuals::ESP_ctx.player->m_angEyeAngles(), headpos, Color(40, 40, 40, 160));
	}
}

void __fastcall Handlers::PaintTraverse_h(void *thisptr, void*, unsigned int vguiPanel, bool forceRepaint, bool allowForce)
{
	if (Global::bShouldUnload)
	{
		Installer::uninstallGladiator();
	}

	static uint32_t HudZoomPanel;
	if (!HudZoomPanel)
		if (!strcmp("HudZoom", g_VGuiPanel->GetName(vguiPanel)))
			HudZoomPanel = vguiPanel;

	if (HudZoomPanel == vguiPanel && g_Options.removals_scope && g_LocalPlayer && g_LocalPlayer->m_hActiveWeapon().Get())
	{
		if (g_LocalPlayer->m_hActiveWeapon().Get()->IsSniper() && g_LocalPlayer->m_bIsScoped())
			return;
	}

	o_PaintTraverse(thisptr, vguiPanel, forceRepaint, allowForce);

	static uint32_t FocusOverlayPanel;
	if (!FocusOverlayPanel)
	{
		const char* szName = g_VGuiPanel->GetName(vguiPanel);

		if (lstrcmpA(szName, "MatSystemTopPanel") == 0)
		{
			FocusOverlayPanel = vguiPanel;

			Visuals::InitFont();

			g_EngineClient->ExecuteClientCmd("clear");
			g_CVar->ConsoleColorPrintf(Color(0, 153, 204, 255), "Exodia \n");

			long res = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, Global::my_documents_folder);
			if (res == S_OK)
			{
				std::string config_folder = std::string(Global::my_documents_folder) + "\\Exodia\\";
				Config::Get().CreateConfigFolder(config_folder);

				std::string default_file_path = config_folder + "Exodia.uwu";

				if (Config::Get().FileExists(default_file_path))
					Config::Get().LoadConfig(default_file_path);
			}

			Skinchanger::Get().LoadSkins();

			g_EngineClient->ExecuteClientCmd("version");
			g_EngineClient->ExecuteClientCmd("toggleconsole");
		}
	}


	if (FocusOverlayPanel == vguiPanel)
	{
		if (g_EngineClient->IsInGame() && g_EngineClient->IsConnected() && g_LocalPlayer)
		{
			ServerSound::Get().Start();
			for (int i = 1; i <= g_EntityList->GetHighestEntityIndex(); i++)
			{
				C_BasePlayer *entity = C_BasePlayer::GetPlayerByIndex(i);
				
				Box *size;
				Color *color;
				if (!entity)
					continue;

				if (i < 65 && Visuals::ValidPlayer(entity))
				{
					if (Visuals::Begin(entity))
					{
						Visuals::RenderFill();
						Visuals::RenderBox();
					
						if (g_Options.esp_player_snaplines) Visuals::RenderSnapline();
						if (g_Options.esp_player_weapons) Visuals::RenderWeapon();
						if (g_Options.esp_AAArrows) Visuals::RenderAAKeyArrows();
						if (g_Options.esp_offscreen) Visuals::RenderOffscreen();
						if (g_Options.esp_player_spreadcross && g_LocalPlayer->IsAlive()) Visuals::DrawSpreadCrosshair();
						if (g_Options.esp_player_name) Visuals::RenderName();
						if (g_Options.esp_player_health) Visuals::RenderHealth();
						if (g_Options.esp_player_skelet) Visuals::RenderSkelet();
						if (g_Options.esp_backtracked_player_skelet) Visuals::RenderBacktrackedSkelet();
						if (g_Options.esp_player_anglelines) Visuals::DrawAngleLines();
						if (g_Options.hvh_resolver) Visuals::DrawResolverModes();

						debug_visuals();						
					}
				}
				else if (g_Options.esp_dropped_weapons && entity->IsWeapon())
					Visuals::RenderWeapon((C_BaseCombatWeapon*)entity);
				else if (entity->IsPlantedC4())
					if (g_Options.esp_planted_c4)
						Visuals::RenderPlantedC4(entity);

				Visuals::RenderNadeEsp((C_BaseCombatWeapon*)entity);
			}
			ServerSound::Get().Finish();

			if (g_Options.removals_scope && (g_LocalPlayer && g_LocalPlayer->m_hActiveWeapon().Get() && g_LocalPlayer->m_hActiveWeapon().Get()->IsSniper() && g_LocalPlayer->m_bIsScoped()))
			{
				int screenX, screenY;
				g_EngineClient->GetScreenSize(screenX, screenY);
				g_VGuiSurface->DrawSetColor(Color::Black);
				g_VGuiSurface->DrawLine(screenX / 2, 0, screenX / 2, screenY);
				g_VGuiSurface->DrawLine(0, screenY / 2, screenX, screenY / 2);
			}

			if (g_Options.misc_spectatorlist)
				Visuals::RenderSpectatorList();

			if (g_Options.visuals_others_grenade_pred)
				CCSGrenadeHint::Get().Paint();

			if (g_Options.visuals_others_hitmarker || g_Options.misc_logevents)
				PlayerHurtEvent::Get().Paint();
		}

		if (g_Options.visuals_others_watermark)
			Visuals::DrawWatermark();
	}
}

bool __stdcall Handlers::CreateMove_h(float smt, CUserCmd *userCMD)
{
	if (!userCMD->command_number || !g_EngineClient->IsInGame() || !g_LocalPlayer || !g_LocalPlayer->IsAlive())
		return o_CreateMove(g_ClientMode, smt, userCMD);



	// Update tickbase correction.
	AimRage::Get().GetTickbase(userCMD);

	// Update lby
	AntiAim::Get().UpdateLBYBreaker(userCMD);

	QAngle org_angle = userCMD->viewangles;

	uintptr_t *framePtr;
	__asm mov framePtr, ebp;

	Global::smt = smt;
	Global::bFakelag = false;
	Global::bSendPacket = true;
	Global::userCMD = userCMD;
	Global::vecUnpredictedVel = g_LocalPlayer->m_vecVelocity();

	if (g_Options.misc_Latency)
		Miscellaneous::Get().FakePing();

	if (g_Options.misc_bhop)
		Miscellaneous::Get().Bhop(userCMD);

	if (g_Options.misc_autostrafe)
		Miscellaneous::Get().AutoStrafe(userCMD);

	if (g_Options.misc_Pushscale)
		Miscellaneous::Get().Pushscale();

	if (g_Options.misc_Slidewalk)
		Miscellaneous::Get().Slidewalk(userCMD);

	if (g_Options.misc_CheatInfo)
		Miscellaneous::Get().CheatInfo(userCMD);

	if (g_Options.misc_Gravity)
		Miscellaneous::Get().Gravity();

	QAngle wish_angle = userCMD->viewangles;
	userCMD->viewangles = org_angle;

	// -----------------------------------------------
	// Do engine prediction
	PredictionSystem::Get().Start(userCMD, g_LocalPlayer);
	{
		if (g_Options.misc_fakewalk)
		{
			AntiAim::Get().Fakewalk(userCMD);
		}
		if (g_Options.misc_fakelag_value)
		{
			Miscellaneous::Get().Fakelag(userCMD);
		}
		Miscellaneous::Get().Instantduck(userCMD);

		Miscellaneous::Get().AutoPistol(userCMD);


		if (g_Options.misc_Slowwalk)
		{
			Miscellaneous::Get().Slowwalk(userCMD);
		}


		AimLegit::Get().Work(userCMD);

		AimRage::Get().Work(userCMD);

		Miscellaneous::Get().AntiAim(userCMD);

		Miscellaneous::Get().FixMovement(userCMD, wish_angle);
	}
	PredictionSystem::Get().End(g_LocalPlayer);

	CCSGrenadeHint::Get().Tick(userCMD->buttons);

	if (g_Options.rage_enabled && Global::bAimbotting && userCMD->buttons & IN_ATTACK)
		*(bool*)(*framePtr - 0x1C) = false;

	*(bool*)(*framePtr - 0x1C) = Global::bSendPacket;

	if (g_Options.hvh_show_real_angles)
	{
		if (!Global::bSendPacket)
			Global::visualAngles = userCMD->viewangles;
	}
	else if(Global::bSendPacket)
		Global::visualAngles = userCMD->viewangles;

	userCMD->forwardmove = Miscellaneous::Get().clamp(userCMD->forwardmove, -450.f, 450.f);
	userCMD->sidemove = Miscellaneous::Get().clamp(userCMD->sidemove, -450.f, 450.f);
	userCMD->upmove = Miscellaneous::Get().clamp(userCMD->upmove, -320.f, 320.f);
	userCMD->viewangles.Clamp();

	if (!g_Options.rage_silent && Global::bVisualAimbotting)
		g_EngineClient->SetViewAngles(Global::vecVisualAimbotAngs);

	if (!o_TempEntities)
	{
		g_pClientStateHook->Setup((uintptr_t*)((uintptr_t)g_ClientState + 0x8));
		g_pClientStateHook->Hook(36, Handlers::TempEntities_h);
		o_TempEntities = g_pClientStateHook->GetOriginal<TempEntities_t>(36);
	}

	return false;
}

void __fastcall Handlers::LockCursor_h(void* ecx, void*)
{
	if (menuOpen)
		o_UnlockCursor(ecx);
	else
		o_LockCursor(ecx);
}

void __stdcall Handlers::PlaySound_h(const char *folderIme)
{
	o_PlaySound(g_VGuiSurface, folderIme);

	if (!g_Options.misc_autoaccept) return;

	if (!strcmp(folderIme, "!UI/competitive_accept_beep.wav"))
	{
		Utils::IsReady();

		FLASHWINFO flash;
		flash.cbSize = sizeof(FLASHWINFO);
		flash.hwnd = window;
		flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
		flash.uCount = 0;
		flash.dwTimeout = 0;
		FlashWindowEx(&flash);
	}
}

HRESULT __stdcall Handlers::EndScene_h(IDirect3DDevice9 *pDevice)
{
	if (!GladiatorMenu::d3dinit)
		GladiatorMenu::GUI_Init(window, pDevice);

	/* This is used because the game calls endscene twice each frame, one for ending the scene and one for finishing textures, this makes it just get called once */
	static auto wanted_ret_address = _ReturnAddress();
	if (_ReturnAddress() == wanted_ret_address)
	{
		//backup render states
		DWORD colorwrite, srgbwrite;
		pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &colorwrite);
		pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbwrite);
			
		// fix drawing without calling engine functons/cl_showpos
		pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
		// removes the source engine color correction
		pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, false);

    	ImGui::GetIO().MouseDrawCursor = menuOpen;

    	ImGui_ImplDX9_NewFrame();
	
		if (menuOpen)
			GladiatorMenu::mainWindow();

		ImGui::Render();

		//restore these
		pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, colorwrite);
		pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);
	}

	ImGuiStyle &style = ImGui::GetStyle();
	if (menuOpen)
	{
		if (style.Alpha > 1.f)
			style.Alpha = 1.f;
		else if (style.Alpha != 1.f)
			style.Alpha += 0.01f;
	}
	else
	{
		if (style.Alpha < 0.f)
			style.Alpha = 0.f;
		else if (style.Alpha != 0.f)
			style.Alpha -= 0.01f;
	}

	if ((g_EngineClient->IsInGame() && g_EngineClient->IsConnected()) && g_Options.misc_revealAllRanks && g_InputSystem->IsButtonDown(KEY_TAB)) // need sanity check, cause called inside EndScene
		Utils::RankRevealAll();

	return o_EndScene(pDevice);
}

HRESULT __stdcall Handlers::Reset_h(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters)
{
	if (!GladiatorMenu::d3dinit)
		return o_Reset(pDevice, pPresentationParameters);

	ImGui_ImplDX9_InvalidateDeviceObjects();

	auto hr = o_Reset(pDevice, pPresentationParameters);

	if (hr == D3D_OK)
	{
		ImGui_ImplDX9_CreateDeviceObjects();
		Visuals::InitFont();
	}

	return hr;
}

LRESULT __stdcall Handlers::WndProc_h(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_LBUTTONDOWN:

		pressedKey[VK_LBUTTON] = true;
		break;

	case WM_LBUTTONUP:

		pressedKey[VK_LBUTTON] = false;
		break;

	case WM_RBUTTONDOWN:

		pressedKey[VK_RBUTTON] = true;
		break;

	case WM_RBUTTONUP:

		pressedKey[VK_RBUTTON] = false;
		break;

	case WM_KEYDOWN:

		pressedKey[wParam] = true;
		break;

	case WM_KEYUP:

		pressedKey[wParam] = false;
		break;

	default: break;
	}

	GladiatorMenu::openMenu();

	if (GladiatorMenu::d3dinit && menuOpen && ImGui_ImplDX9_WndProcHandler(hWnd, uMsg, wParam, lParam) && !input_shouldListen)
		return true;

	return CallWindowProc(oldWindowProc, hWnd, uMsg, wParam, lParam);
}

ConVar* r_DrawSpecificStaticProp, *fog_enable, *fog_enable_water_fog, *fog_enableskybox;



void __stdcall Handlers::FrameStageNotify_h(ClientFrameStage_t stage)
{

	static bool OldNightmode;
	static bool OldAsuswall;
	static int OldNightcolor; //not even near p but ''p in theory'' methods doesnt work for some unknown reason.
	int nightcolor = (g_Options.visuals_others_nightmode_color[0] + g_Options.visuals_others_nightmode_color[1] + g_Options.visuals_others_nightmode_color[2]) * 100;//blaim microsoft



	g_LocalPlayer = C_BasePlayer::GetLocalPlayer(true);

	if (!g_LocalPlayer || !g_EngineClient->IsInGame() || !g_EngineClient->IsConnected())
		return o_FrameStageNotify(stage);

	QAngle aim_punch_old;
	QAngle view_punch_old;

	QAngle *aim_punch = nullptr;
	QAngle *view_punch = nullptr;

	if (stage == ClientFrameStage_t::FRAME_NET_UPDATE_POSTDATAUPDATE_START)
	{
		if (g_Options.skinchanger_enabled)
			Skinchanger::Get().Work();

		Miscellaneous::Get().PunchAngleFix_FSN();
	}
	if ((OldNightmode != g_Options.visuals_others_nightmode || OldNightcolor != nightcolor || OldAsuswall != g_Options.visuals_others_asuswall))
	{

		if (!r_DrawSpecificStaticProp)
			r_DrawSpecificStaticProp = g_CVar->FindVar("r_DrawSpecificStaticProp");
		if (!fog_enable)
			fog_enable = g_CVar->FindVar("fog_enable");
		if (!fog_enableskybox)
			fog_enableskybox = g_CVar->FindVar("fog_enableskybox");
		if (!fog_enable_water_fog)
			fog_enable_water_fog = g_CVar->FindVar("fog_enable_water_fog");

		r_DrawSpecificStaticProp->SetValue(0);
		fog_enable->SetValue(0);
		fog_enableskybox->SetValue(0);
		fog_enable_water_fog->SetValue(0);

		for (MaterialHandle_t i = g_MatSystem->FirstMaterial(); i != g_MatSystem->InvalidMaterial(); i = g_MatSystem->NextMaterial(i))
		{
			if (i == 0)		//i = 0 == crash? 687 and 1164 possably too, needs more testing tho
				continue;

			IMaterial* pMaterial = g_MatSystem->GetMaterial(i);

			if (!pMaterial)
				continue;

			if (strstr(pMaterial->GetTextureGroupName(), "World") || strstr(pMaterial->GetTextureGroupName(), "StaticProp") || strstr(pMaterial->GetTextureGroupName(), "Model") || strstr(pMaterial->GetTextureGroupName(), "Decal") || strstr(pMaterial->GetTextureGroupName(), "Unaccounted") || strstr(pMaterial->GetTextureGroupName(), "Morph") || strstr(pMaterial->GetTextureGroupName(), "Lighting") || (strstr(pMaterial->GetTextureGroupName(), "SkyBox")))
				//if (!((strstr(pMaterial->GetTextureGroupName(), "SkyBox") && !colormod_skybox) || strstr(pMaterial->GetTextureGroupName(), "Other") || strstr(pMaterial->GetTextureGroupName(), "Pixel Shaders") || strstr(pMaterial->GetTextureGroupName(), "ClientEffect") || strstr(pMaterial->GetTextureGroupName(), "Lighting")) || strstr(pMaterial->GetTextureGroupName(), "de_") || strstr(pMaterial->GetTextureGroupName(), "cs_"))
			{
				if (g_Options.visuals_others_nightmode)
				{
					if (strstr(pMaterial->GetTextureGroupName(), "Lighting"))
						pMaterial->ColorModulate(2.0f - g_Options.visuals_others_nightmode_color[0], 2.0f - g_Options.visuals_others_nightmode_color[1], 2.0f - g_Options.visuals_others_nightmode_color[2]);
					else if ((strstr(pMaterial->GetTextureGroupName(), "StaticProp") || (strstr(pMaterial->GetTextureGroupName(), "Model"))) && !(strstr(pMaterial->GetName(), "player") || strstr(pMaterial->GetName(), "chams") || strstr(pMaterial->GetName(), "debug/debugdrawflat")))
						pMaterial->ColorModulate(min(1.0f, pow(g_Options.visuals_others_nightmode_color[0] - 1, 2) * -0.8 + 1), min(1.0f, pow(g_Options.visuals_others_nightmode_color[1] - 1, 2) * -0.8 + 1), min(1.0f, pow(g_Options.visuals_others_nightmode_color[2] - 1, 2) * -0.8 + 1));
					else if (!(strstr(pMaterial->GetName(), "player") || strstr(pMaterial->GetName(), "chams") || strstr(pMaterial->GetName(), "debug/debugdrawflat")))
						pMaterial->ColorModulate(g_Options.visuals_others_nightmode_color[0], g_Options.visuals_others_nightmode_color[1], g_Options.visuals_others_nightmode_color[2]);
				}
				else
					pMaterial->ColorModulate(1.0f, 1.0f, 1.0f);
				if (g_Options.visuals_others_asuswall)
				{
					if (strstr(pMaterial->GetTextureGroupName(), "StaticProp"))
						pMaterial->AlphaModulate(g_Options.visuals_others_nightmode_color[3]);
				}
				else
					pMaterial->AlphaModulate(1.0f);
			}
		}

		OldNightmode = g_Options.visuals_others_nightmode;
		OldAsuswall = g_Options.visuals_others_asuswall;
		OldNightcolor = nightcolor;
	}
	if (stage == ClientFrameStage_t::FRAME_NET_UPDATE_POSTDATAUPDATE_END)
	{
		for (int i = 1; i < g_EntityList->GetHighestEntityIndex(); i++)
		{
			C_BasePlayer *player = C_BasePlayer::GetPlayerByIndex(i);

			if (!player)
				continue;

			if (player == g_LocalPlayer)
				continue;

			if (!player->IsAlive())
				continue;

			if (player->IsTeamMate())
				continue;

			VarMapping_t *map = player->VarMapping();
			if (map)
			{
				for (int j = 0; j < map->m_nInterpolatedEntries; j++)
				{
					map->m_Entries[j].m_bNeedsToInterpolate = !g_Options.rage_lagcompensation;
				}
			}
		}

		if (g_Options.hvh_resolver)
			Resolver::Get().Resolve();
	}

	if (stage == ClientFrameStage_t::FRAME_RENDER_START)
	{
		*(bool*)Offsets::bOverridePostProcessingDisable = g_Options.removals_postprocessing;

		if (g_LocalPlayer->IsAlive())
		{
			static ConVar *default_skyname = g_CVar->FindVar("sv_skyname");
			static int iOldSky = 0;

			if (iOldSky != g_Options.visuals_others_sky)
			{
				Utils::LoadNamedSkys(g_Options.visuals_others_sky == 0 ? default_skyname->GetString() : opt_Skynames[g_Options.visuals_others_sky]);
				iOldSky = g_Options.visuals_others_sky;
			}

			if (g_Options.removals_novisualrecoil)
			{
				aim_punch = &g_LocalPlayer->m_aimPunchAngle();
				view_punch = &g_LocalPlayer->m_viewPunchAngle();

				aim_punch_old = *aim_punch;
				view_punch_old = *view_punch;

				*aim_punch = QAngle(0.f, 0.f, 0.f);
				*view_punch = QAngle(0.f, 0.f, 0.f);
			}

			if (g_Input->m_fCameraInThirdPerson)
				g_LocalPlayer->visuals_Angles() = Global::visualAngles;

			if (g_Options.removals_smoke)
				*(int*)Offsets::smokeCount = 0;
		}

		if (g_Options.removals_flash && g_LocalPlayer)
			if (g_LocalPlayer->m_flFlashDuration() > 0.f)
				g_LocalPlayer->m_flFlashDuration() = 0.f;
	}

	o_FrameStageNotify(stage);

	if (stage == ClientFrameStage_t::FRAME_NET_UPDATE_END)
	{
		if (g_Options.hvh_resolver)
			Resolver::Get().Log();

		if (g_Options.rage_lagcompensation)
			CMBacktracking::Get().FrameUpdatePostEntityThink();
	}

	if (stage == ClientFrameStage_t::FRAME_RENDER_START)
	{
		if (g_LocalPlayer && g_LocalPlayer->IsAlive())
		{
			if (g_Options.removals_novisualrecoil && (aim_punch && view_punch))
			{
				*aim_punch = aim_punch_old;
				*view_punch = view_punch_old;
			}
		}
	}
}

bool __fastcall Handlers::FireEventClientSide_h(void *thisptr, void*, IGameEvent *gEvent)
{

	if (!gEvent)
		return o_FireEventClientSide(thisptr, gEvent);

	if (strcmp(gEvent->GetName(), "game_newmap") == 0)
	{
		static ConVar *default_skyname = g_CVar->FindVar("sv_skyname");
		Utils::LoadNamedSkys(g_Options.visuals_others_sky == 0 ? default_skyname->GetString() : opt_Skynames[g_Options.visuals_others_sky]);
		Global::netchan = nullptr;
	}

	return o_FireEventClientSide(thisptr, gEvent);
}

void __fastcall Handlers::IsConnected()
{


	static void* unk = Utils::PatternScan(GetModuleHandle("client_panorama.dll"), "75 04 B0 01 5F") - 2;

	if (_ReturnAddress() == unk && g_Options.misc_Inv)
	{
		return;
	}
}

void __fastcall Handlers::BeginFrame_h(void *thisptr, void*, float ft)
{
	Miscellaneous::Get().WorldMod();
	Miscellaneous::Get().FakePing();
	Miscellaneous::Get().NameChanger();
	Miscellaneous::Get().ChatSpamer();
	Miscellaneous::Get().ClanTag();
	BulletImpactEvent::Get().Paint();

	o_BeginFrame(thisptr, ft);
}

void __fastcall Handlers::SetKeyCodeState_h(void* thisptr, void* EDX, ButtonCode_t code, bool bDown)
{
	if (input_shouldListen && bDown)
	{
		input_shouldListen = false;
		if (input_receivedKeyval)
			*input_receivedKeyval = code;
	}

	return o_SetKeyCodeState(thisptr, code, bDown);
}

void __fastcall Handlers::SetMouseCodeState_h(void* thisptr, void* EDX, ButtonCode_t code, MouseCodeState_t state)
{
	if (input_shouldListen && state == BUTTON_PRESSED)
	{
		input_shouldListen = false;
		if (input_receivedKeyval)
			*input_receivedKeyval = code;
	}

	return o_SetMouseCodeState(thisptr, code, state);
}

void __stdcall Handlers::OverrideView_h(CViewSetup* pSetup)
{
	// Do no zoom aswell.

	static float orifov = pSetup->fov;



	g_Options.removals_Zoomfix ? pSetup->fov = orifov + g_Options.visuals_others_player_fov : pSetup->fov += g_Options.visuals_others_player_fov;
	if (g_Options.removals_Zoomfix && g_EngineClient->IsInGame())
		g_CVar->FindVar("zoom_sensitivity_ratio_mouse")->SetValue(0);
	else
		g_CVar->FindVar("zoom_sensitivity_ratio_mouse")->SetValue(1);



	pSetup->fov += g_Options.visuals_others_player_fov;

	o_OverrideView(pSetup);

	if (g_EngineClient->IsInGame() && g_EngineClient->IsConnected())
	{
		if (g_LocalPlayer)
		{
			CCSGrenadeHint::Get().View();

			Miscellaneous::Get().ThirdPerson();
		}
	}
}

void Proxies::didSmokeEffect(const CRecvProxyData *pData, void *pStruct, void *pOut)
{
	if (g_Options.removals_smoke_type == 0)
		*(bool*)((DWORD)pOut + 0x1) = true;

	std::vector<const char*> wireframesmoke_mats =
	{
		"particle/vistasmokev1/vistasmokev1_emods",
		"particle/vistasmokev1/vistasmokev1_emods_impactdust",
		"particle/vistasmokev1/vistasmokev1_fire",
		"particle/vistasmokev1/vistasmokev1_smokegrenade",
	};

	if (g_Options.removals_smoke_type == 1)
	{
		for (auto smoke_mat : wireframesmoke_mats)
		{
			IMaterial* mat = g_MatSystem->FindMaterial(smoke_mat, TEXTURE_GROUP_OTHER);
			mat->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME, true);
		}
	}

	o_didSmokeEffect(pData, pStruct, pOut);
}

bool __stdcall Handlers::InPrediction_h()
{
	if (g_Options.rage_fixup_entities)
	{
		// Breaks more than it fixes.
		//// xref : "%8.4f : %30s : %5.3f : %4.2f  +\n" https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/game/client/c_baseanimating.cpp#L1808
		//static DWORD inprediction_check = (DWORD)Utils::PatternScan(GetModuleHandle("client_panorama.dll"), "84 C0 74 17 8B 87");
		//if (inprediction_check == (DWORD)_ReturnAddress()) {
		//	return true; // no sequence transition / decay
		//}
	}

	return o_OriginalInPrediction(g_Prediction);
}

bool __fastcall Handlers::SetupBones_h(void* ECX, void* EDX, matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	// Supposed to only setupbones tick by tick, instead of frame by frame.
	if (g_Options.rage_lagcompensation)
	{
		if (ECX && ((IClientRenderable*)ECX)->GetIClientUnknown())
		{
			IClientNetworkable* pNetworkable = ((IClientRenderable*)ECX)->GetIClientUnknown()->GetClientNetworkable();
			if (pNetworkable && pNetworkable->GetClientClass() && pNetworkable->GetClientClass()->m_ClassID == ClassId::ClassId_CCSPlayer)
			{
				static auto host_timescale = g_CVar->FindVar(("host_timescale"));
				auto player = (C_BasePlayer*)ECX;
				float OldCurTime = g_GlobalVars->curtime;
				float OldRealTime = g_GlobalVars->realtime;
				float OldFrameTime = g_GlobalVars->frametime;
				float OldAbsFrameTime = g_GlobalVars->absoluteframetime;
				float OldAbsFrameTimeStart = g_GlobalVars->absoluteframestarttimestddev;
				float OldInterpAmount = g_GlobalVars->interpolation_amount;
				int OldFrameCount = g_GlobalVars->framecount;
				int OldTickCount = g_GlobalVars->tickcount;

				g_GlobalVars->curtime = player->m_flSimulationTime();
				g_GlobalVars->realtime = player->m_flSimulationTime();
				g_GlobalVars->frametime = g_GlobalVars->interval_per_tick * host_timescale->GetFloat();
				g_GlobalVars->absoluteframetime = g_GlobalVars->interval_per_tick * host_timescale->GetFloat();
				g_GlobalVars->absoluteframestarttimestddev = player->m_flSimulationTime() - g_GlobalVars->interval_per_tick * host_timescale->GetFloat();
				g_GlobalVars->interpolation_amount = 0;
				g_GlobalVars->framecount = TIME_TO_TICKS(player->m_flSimulationTime());
				g_GlobalVars->tickcount = TIME_TO_TICKS(player->m_flSimulationTime());

				*(int*)((int)player + 236) |= 8; // IsNoInterpolationFrame
				bool ret_value = o_SetupBones(player, pBoneToWorldOut, nMaxBones, boneMask, g_GlobalVars->curtime);
				*(int*)((int)player + 236) &= ~8; // (1 << 3)

				g_GlobalVars->curtime = OldCurTime;
				g_GlobalVars->realtime = OldRealTime;
				g_GlobalVars->frametime = OldFrameTime;
				g_GlobalVars->absoluteframetime = OldAbsFrameTime;
				g_GlobalVars->absoluteframestarttimestddev = OldAbsFrameTimeStart;
				g_GlobalVars->interpolation_amount = OldInterpAmount;
				g_GlobalVars->framecount = OldFrameCount;
				g_GlobalVars->tickcount = OldTickCount;
				return ret_value;
			}
		}
	}
	return o_SetupBones(ECX, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
}

void __fastcall Handlers::SceneEnd_h(void* thisptr, void* edx)
{
	if (!g_LocalPlayer || !g_EngineClient->IsInGame() || !g_EngineClient->IsConnected())
		return o_SceneEnd(thisptr);

	o_SceneEnd(thisptr);


		constexpr float color_gray[4] = { 166, 167, 169, 255 };
		
			for (int i = 1; i < g_GlobalVars->maxClients; ++i) {
			auto ent = static_cast<C_BasePlayer*>(g_EntityList->GetClientEntity(i));
			if (ent && ent->IsAlive() && !ent->IsDormant()) {

				auto player = C_BasePlayer::GetPlayerByIndex(i);
				C_BasePlayer *entity = C_BasePlayer::GetPlayerByIndex(i);
				bool is_enemy = entity->m_iTeamNum() != g_LocalPlayer->m_iTeamNum();

				IMaterial *mat = g_Options.esp_player_chams_type < 2 ? g_MatSystem->FindMaterial("chams", TEXTURE_GROUP_MODEL) : g_MatSystem->FindMaterial("debug/debugdrawflat", TEXTURE_GROUP_MODEL);


				if (!mat || mat->IsErrorMaterial())
					return;

				if (!is_enemy && g_Options.esp_enemies_only)
					continue;

				if (!player->IsTeamMate() && g_LocalPlayer->m_iTeamNum() && g_Options.esp_only_enemies)
					continue;

				if (g_Options.esp_player_chams_type == 1 || g_Options.esp_player_chams_type == 3)
				{	// XQZ Chams
					g_RenderView->SetColorModulation(ent->m_bGunGameImmunity() ? color_gray : (ent->m_iTeamNum() == 2 ? g_Options.esp_player_chams_color_t : g_Options.esp_player_chams_color_ct));

					mat->IncrementReferenceCount();
					mat->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ, true);

					g_MdlRender->ForcedMaterialOverride(mat);

					ent->DrawModel(0x1, 255);
					g_MdlRender->ForcedMaterialOverride(nullptr);

					g_RenderView->SetColorModulation(ent->m_bGunGameImmunity() ? color_gray : (ent->m_iTeamNum() == 2 ? g_Options.esp_player_chams_color_t_visible : g_Options.esp_player_chams_color_ct_visible));

					mat->IncrementReferenceCount();
					mat->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ, false);

					g_MdlRender->ForcedMaterialOverride(mat);

					ent->DrawModel(0x1, 255);
					g_MdlRender->ForcedMaterialOverride(nullptr);
				}
				else if (g_Options.esp_player_chams_type == 4)
				{
					g_RenderView->SetColorModulation(ent->m_bGunGameImmunity() ? color_gray : (ent->m_iTeamNum() == 2 ? g_Options.esp_player_chams_color_t : g_Options.esp_player_chams_color_ct));

					mat->IncrementReferenceCount();
					mat->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME, true);

					g_MdlRender->ForcedMaterialOverride(mat);

					ent->DrawModel(0x1, 255);
					g_MdlRender->ForcedMaterialOverride(nullptr);

					g_RenderView->SetColorModulation(ent->m_bGunGameImmunity() ? color_gray : (ent->m_iTeamNum() == 2 ? g_Options.esp_player_chams_color_t_wire : g_Options.esp_player_chams_color_ct_wire));

					mat->IncrementReferenceCount();
					mat->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME, false);

					g_MdlRender->ForcedMaterialOverride(mat);

					ent->DrawModel(0x1, 255);
					g_MdlRender->ForcedMaterialOverride(nullptr);
				}
				else
				{	// Normal Chams

					if (g_Options.esp_player_chams_type == 5)
					{
						g_RenderView->SetColorModulation(ent->m_iTeamNum() == 2 ? g_Options.esp_player_chams_color_t_colored : g_Options.esp_player_chams_color_ct_colored);
					}
					else
					{
						g_RenderView->SetColorModulation(ent->m_iTeamNum() == 2 ? g_Options.esp_player_chams_color_t_visible : g_Options.esp_player_chams_color_ct_visible);
					}


					g_MdlRender->ForcedMaterialOverride(mat);

					if (g_Options.visuals_transcheck)
					{
						mat->AlphaModulate(g_Options.visuals_trans);
					}

					else

					{
						mat->AlphaModulate(1.f);
					}

					ent->DrawModel(0x1, 255);

					g_MdlRender->ForcedMaterialOverride(nullptr);
				}
			
			

		}
	}

	if (g_Options.glow_enabled)
		Glow::RenderGlow();

//Needs a fix
#ifdef NIGHTMODE
	static bool bPerformed = true;
	static bool bLastSetting;
	static float old_colors[4] = { *g_Options.visuals_others_nightmode_color };

	if (!bPerformed || old_colors != g_Options.visuals_others_nightmode_color)
	{
		for (auto i = g_MatSystem->FirstMaterial(); i != g_MatSystem->InvalidMaterial(); i = g_MatSystem->NextMaterial(i))
		{
			auto pMat = g_MatSystem->GetMaterial(i);

			if (!pMat || pMat->IsErrorMaterial())
				continue;

			if (strstr(pMat->GetTextureGroupName(), "World") || strstr(pMat->GetTextureGroupName(), "StaticProp"))
			{
				if (bLastSetting)
				{
					pMat->ColorModulate(g_Options.visuals_others_nightmode_color[0], g_Options.visuals_others_nightmode_color[1], g_Options.visuals_others_nightmode_color[2]);
				}
				else
				{
					pMat->ColorModulate(1.f, 1.f, 1.f);
					pMat->AlphaModulate(1.f);
				}
			}
		}
		bPerformed = true;
	}
	if (bLastSetting != g_Options.visuals_others_nightmode)
	{
		bLastSetting = g_Options.visuals_others_nightmode;
		bPerformed = false;
	}
#endif

}

void __stdcall FireBullets_PostDataUpdate(C_TEFireBullets *thisptr, DataUpdateType_t updateType)
{
	if (!g_LocalPlayer || !g_LocalPlayer->IsAlive())
		return o_FireBullets(thisptr, updateType);

	if (g_Options.rage_lagcompensation && thisptr)
	{
		int iPlayer = thisptr->m_iPlayer + 1;
		if (iPlayer < 64)
		{
			auto player = C_BasePlayer::GetPlayerByIndex(iPlayer);
			
			if (player && player != g_LocalPlayer && !player->IsDormant() && !player->IsTeamMate())
			{
				QAngle eyeAngles = QAngle(thisptr->m_vecAngles.pitch, thisptr->m_vecAngles.yaw, thisptr->m_vecAngles.roll);
				QAngle calcedAngle = Math::CalcAngle(player->GetEyePos(), g_LocalPlayer->GetEyePos());
				
				thisptr->m_vecAngles.pitch = calcedAngle.pitch;
				thisptr->m_vecAngles.yaw = calcedAngle.yaw;
				thisptr->m_vecAngles.roll = 0.f;

				float
					event_time = g_GlobalVars->tickcount,
					player_time = player->m_flSimulationTime();

				// Extrapolate tick to hit scouters etc
				auto lag_records = CMBacktracking::Get().m_LagRecord[iPlayer];

				float shot_time = TICKS_TO_TIME(event_time);
				for (auto& record : lag_records)
				{
					if (record.m_iTickCount <= event_time)
					{
						shot_time = record.m_flSimulationTime + TICKS_TO_TIME(event_time - record.m_iTickCount); // also get choked from this

						g_CVar->ConsoleColorPrintf(Color(0, 255, 0, 255), "Found <<exact>> shot time: %f, ticks choked to get here: %d\n", shot_time, event_time - record.m_iTickCount);

						break;
					}

					else
						g_CVar->ConsolePrintf("Bad curtime difference, EVENT: %f, RECORD: %f\n", event_time, record.m_iTickCount);

				}
//#ifdef _DEBUG
				g_CVar->ConsolePrintf("Calced angs: %f %f, Event angs: %f %f, CURTIME_TICKOUNT: %f, SIMTIME: %f, CALCED_TIME: %f\n", calcedAngle.pitch, calcedAngle.yaw, eyeAngles.pitch, eyeAngles.yaw, event_time, player_time, shot_time);
//#endif
				/*if (!lag_records.empty())
				{
					int choked = floorf((event_time - player_time) / g_GlobalVars->interval_per_tick) + 0.5;
					choked = (choked > 14 ? 14 : choked < 1 ? 0 : choked);
					player->m_vecOrigin() = (lag_records.begin()->m_vecOrigin + (g_GlobalVars->interval_per_tick * lag_records.begin()->m_vecVelocity * choked));
				}*/

				CMBacktracking::Get().SetOverwriteTick(player, calcedAngle, shot_time, 1);
			}
		}
	}

	o_FireBullets(thisptr, updateType);
}

__declspec (naked) void __stdcall Handlers::TEFireBulletsPostDataUpdate_h(DataUpdateType_t updateType)
{
	__asm
	{
		push[esp + 4]
		push ecx
		call FireBullets_PostDataUpdate
		retn 4
	}
}

bool __fastcall Handlers::TempEntities_h(void* ECX, void* EDX, void* msg)
{
	if (!g_LocalPlayer || !g_EngineClient->IsInGame() || !g_EngineClient->IsConnected())
		return o_TempEntities(ECX, msg);

	bool ret = o_TempEntities(ECX, msg);

	auto CL_ParseEventDelta = [](void *RawData, void *pToData, RecvTable *pRecvTable)
	{
		// "RecvTable_DecodeZeros: table '%s' missing a decoder.", look at the function that calls it.
		static uintptr_t CL_ParseEventDeltaF = (uintptr_t)Utils::PatternScan(GetModuleHandle("engine.dll"), ("55 8B EC 83 E4 F8 53 57"));
		__asm
		{
			mov     ecx, RawData
			mov     edx, pToData
			push	pRecvTable
			call    CL_ParseEventDeltaF
			add     esp, 4
		}
	};

	// Filtering events
	if (!g_Options.rage_lagcompensation || !g_LocalPlayer->IsAlive())
		return ret;

	CEventInfo *ei = g_ClientState->events;
	CEventInfo *next = NULL;

	if (!ei)
		return ret;

	do
	{
		next = *(CEventInfo**)((uintptr_t)ei + 0x38);

		uint16_t classID = ei->classID - 1;

		auto m_pCreateEventFn = ei->pClientClass->m_pCreateEventFn; // ei->pClientClass->m_pCreateEventFn ptr
		if (!m_pCreateEventFn)
			continue;

		IClientNetworkable *pCE = m_pCreateEventFn();
		if (!pCE)
			continue;

		if (classID == ClassId::ClassId_CTEFireBullets)
		{
			// set fire_delay to zero to send out event so its not here later.
			ei->fire_delay = 0.0f;

//			auto pRecvTable = ei->pClientClass->m_pRecvTable;
//			void *BasePtr = pCE->GetDataTableBasePtr();
//
//			// Decode data into client event object and use the DTBasePtr to get the netvars
//			CL_ParseEventDelta(ei->pData, BasePtr, pRecvTable);
//
//			if (!BasePtr)
//				continue;
//
//			// This nigga right HERE just fired a BULLET MANE
//			int EntityIndex = *(int*)((uintptr_t)BasePtr + 0x10) + 1;
//
//			auto pEntity = (C_BasePlayer*)g_EntityList->GetClientEntity(EntityIndex);
//			if (pEntity && pEntity->GetClientClass() &&  pEntity->GetClientClass()->m_ClassID == ClassId::ClassId_CCSPlayer && !pEntity->IsTeamMate())
//			{
//				QAngle EyeAngles = QAngle(*(float*)((uintptr_t)BasePtr + 0x24), *(float*)((uintptr_t)BasePtr + 0x28), 0.0f),
//					CalcedAngle = Math::CalcAngle(pEntity->GetEyePos(), g_LocalPlayer->GetEyePos());
//
//				*(float*)((uintptr_t)BasePtr + 0x24) = CalcedAngle.pitch;
//				*(float*)((uintptr_t)BasePtr + 0x28) = CalcedAngle.yaw;
//				*(float*)((uintptr_t)BasePtr + 0x2C) = 0;
//
//				float
//					event_time = TICKS_TO_TIME(g_GlobalVars->tickcount),
//					player_time = pEntity->m_flSimulationTime();
//
//				// Extrapolate tick to hit scouters etc
//				auto lag_records = CMBacktracking::Get().m_LagRecord[pEntity->EntIndex()];
//
//				float shot_time = event_time;
//				for (auto& record : lag_records)
//				{
//					if (TICKS_TO_TIME(record.m_iTickCount) <= event_time)
//					{
//						shot_time = record.m_flSimulationTime + (event_time - TICKS_TO_TIME(record.m_iTickCount)); // also get choked from this
//#ifdef _DEBUG
//						g_CVar->ConsoleColorPrintf(Color(0, 255, 0, 255), "Found exact shot time: %f, ticks choked to get here: %d\n", shot_time, TIME_TO_TICKS(event_time - TICKS_TO_TIME(record.m_iTickCount)));
//#endif
//						break;
//					}
//#ifdef _DEBUG
//					else
//						g_CVar->ConsolePrintf("Bad curtime difference, EVENT: %f, RECORD: %f\n", event_time, TICKS_TO_TIME(record.m_iTickCount));
//#endif
//				}
//#ifdef _DEBUG
//				g_CVar->ConsolePrintf("Calced angs: %f %f, Event angs: %f %f, CURTIME_TICKOUNT: %f, SIMTIME: %f, CALCED_TIME: %f\n", CalcedAngle.pitch, CalcedAngle.yaw, EyeAngles.pitch, EyeAngles.yaw, event_time, player_time, shot_time);
//#endif
//				if (!lag_records.empty())
//				{
//					int choked = floorf((event_time - player_time) / g_GlobalVars->interval_per_tick) + 0.5;
//					choked = (choked > 14 ? 14 : choked < 1 ? 0 : choked);
//					pEntity->m_vecOrigin() = (lag_records.begin()->m_vecOrigin + (g_GlobalVars->interval_per_tick * lag_records.begin()->m_vecVelocity * choked));
//				}
//
//				CMBacktracking::Get().SetOverwriteTick(pEntity, CalcedAngle, shot_time, 1);
//			}
		}
		ei = next;
	} while (next != NULL);

	return ret;
}

float __fastcall Handlers::GetViewModelFov_h(void* ECX, void* EDX)
{
	return g_Options.visuals_others_player_fov_viewmodel + o_GetViewmodelFov(ECX);
}

bool __fastcall Handlers::GetBool_SVCheats_h(PVOID pConVar, int edx)
{
	// xref : "Pitch: %6.1f   Yaw: %6.1f   Dist: %6.1f %16s"
	static DWORD CAM_THINK = (DWORD)Utils::PatternScan(GetModuleHandle("client_panorama.dll"), "85 C0 75 30 38 86");
	if (!pConVar)
		return false;

	if (g_Options.misc_thirdperson)
	{
		if ((DWORD)_ReturnAddress() == CAM_THINK)
			return true;
	}

	return o_GetBool(pConVar);
}

void __fastcall Handlers::RunCommand_h(void* ECX, void* EDX, C_BasePlayer* player, CUserCmd* cmd, IMoveHelper* helper)
{
	o_RunCommand(ECX, player, cmd, helper);

	Miscellaneous::Get().PunchAngleFix_RunCommand(player);
}

int __fastcall Handlers::SendDatagram_h(INetChannel *ECX, void *EDX, bf_write *data)
{

	if (data || g_Options.misc_LatencySlider < ECX->GetLatency(FLOW_INCOMING) + ECX->GetLatency(FLOW_OUTGOING))
		return o_SendDatagram(ECX, data);

	int instate = ECX->m_nInReliableState;
	int outstate = ECX->m_nOutReliableState;
	int insequencenr = ECX->m_nInSequenceNr;

	CMBacktracking::Get().AddLatency2(ECX, g_Options.misc_LatencySlider);

	INetChannel *net_channel = (INetChannel*)ECX;

	int32_t reliable_state = net_channel->m_nInReliableState;
	int32_t sequencenr = net_channel->m_nInSequenceNr;

	int ret = o_SendDatagram(net_channel, data);

	net_channel->m_nInReliableState = reliable_state;
	net_channel->m_nInSequenceNr = sequencenr;

	return ret;
}

int __stdcall Handlers::IsBoxVisible_h(const Vector &mins, const Vector &maxs)
{
	if (!memcmp(_ReturnAddress(), "\x85\xC0\x74\x2D\x83\x7D\x10\x00\x75\x1C", 10))
		return 1;

	return o_IsBoxVisible(mins, maxs);
}

bool __fastcall Handlers::IsHLTV_h(void *ECX, void *EDX)
{
	uintptr_t player;
	__asm
	{
		mov player, edi
	}

	if ((DWORD)_ReturnAddress() != Offsets::reevauluate_anim_lod)
		return o_IsHLTV(ECX);

	if (!player || player == 0x000FFFF)
		return o_IsHLTV(ECX);

	/*if (player % 10 == 0x4)
		player -= 4;*/

	*(int32_t*)(player + 0xA24) = -1;
	*(int32_t*)(player + 0xA2C) = *(int32_t*)(player + 0xA28);
	*(int32_t*)(player + 0xA28) = 0;

	return true;
}