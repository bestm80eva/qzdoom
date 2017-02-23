// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2017 Rachael Alexanderson
//
// **Uses lots of code from the following authors:
// Copyright 1998-2017 Randy Heit
// Copyright 2017 Christoph Oelckers
// Copyright(C) 2015-2017 Christopher Bruns
// Copyright (c) 2016-2017 Magnus Norddahl
//
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** splitscreen.cpp
** Lots and lots of splitscreen stuff
**
**/


// includes

#include "gl/system/gl_system.h"
#include "gl/system/gl_framebuffer.h"
#include "c_dispatch.h"
#include "d_player.h"
#include "d_net.h"
#include "d_ticcmd.h"
#include "d_event.h"
#include "doomstat.h"
#include "m_joy.h"
#include "gi.h"
#include "g_game.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "g_statusbar/sbar.h"
#include "g_statusbar/sbarinfo.h"
#include "p_effect.h"
#include "r_state.h"
#include "r_utility.h"
#include "r_data/r_interpolate.h"
#include "gl/dynlights/gl_lightbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "gl/renderer/gl_renderbuffers.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_2ddrawer.h"
#include "gl/renderer/gl_postprocessstate.h"
#include "gl/scene/gl_clipper.h"
#include "gl/stereo3d/gl_stereo3d.h"
#include "gl/stereo3d/scoped_view_shifter.h"
#include "teaminfo.h"


// external cvars

EXTERN_CVAR(Bool, splitscreen)
EXTERN_CVAR(Int, vr_mode)
EXTERN_CVAR(Int, ss_mode)
EXTERN_CVAR(Bool, r_deathcamera)
EXTERN_CVAR(Bool, cl_run)
EXTERN_CVAR(Bool, lookstrafe)
EXTERN_CVAR(Bool, freelook)
EXTERN_CVAR(Float, m_forward)
EXTERN_CVAR(Float, m_side)


// external function prototypes

void D_Display();
void G_BuildTiccmd(ticcmd_t* cmd);
void V_FixAspectSettings();


// forward function prototypes

void G_BuildTiccmd_Split(ticcmd_t* cmd, ticcmd_t* cmd2);


// external global variables
extern bool NoInterpolateView;
extern short consistancy[MAXPLAYERS][BACKUPTICS];
extern int turnheld;
extern int forwardmove[2], sidemove[2], angleturn[4], lookspeed[2], flyspeed[2];
extern int mousex, mousey;
extern bool SendLand, sendpause, sendsave, sendturn180;
extern FString savegamefile, savedescription;


// defines...?
#define SLOWTURNTICS	6 
#define MAXPLMOVE				(forwardmove[1]) 


// forward static
static inline int joyint(double val)
{
	if (val >= 0)
	{
		return int(ceil(val));
	}
	else
	{
		return int(floor(val));
	}
}



//-----------------------------------------------------------------------------
//
// Alternative to D_Display () for splitscreen. In fact, it calls it, but
// with some restrictions. ;)
//
//-----------------------------------------------------------------------------

void FGLRenderer::SplitDisplays()
{
	if (!this)
	{
		splitscreen = false;
		return D_Display();
	}

	//static gamestate_t lastgamestate = gamestate;
	int oldcp = consoleplayer;
	int oldvr = vr_mode;
	DBaseStatusBar *OldStatusBar = StatusBar;

	if (vr_mode == 0)
		vr_mode = ss_mode;

	const s3d::Stereo3DMode& stereo3dMode = s3d::Stereo3DMode::getCurrentMode();

	if (gamestate == GS_LEVEL || gamestate == GS_TITLELEVEL)
		Renderer->RenderView(&players[consoleplayer]);

	//vr_mode = 0;
	for (int player = 0;player<2;player++)
	{
		if (consoleplayer == -1)
			consoleplayer = oldcp;

		if (gamestate == GS_LEVEL || gamestate == GS_TITLELEVEL)
		{
			if (StatusBar && &players[consoleplayer] && &players[consoleplayer].camera && &players[consoleplayer].camera->player)
			{
				StatusBar->AttachToPlayer(players[consoleplayer].camera->player);
				StatusBar->Tick();
			}
		}

		D_Display ();

		mBuffers->BindEyeFB(player);
		
		glViewport(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);
		glScissor(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);
		m2DDrawer->Draw();
		m2DDrawer->Clear();

		consoleplayer = consoleplayer2;
		StatusBar = StatusBar2;
	}
	vr_mode = oldvr;
	consoleplayer = oldcp;
	StatusBar = OldStatusBar;

	FGLPostProcessState savedState;
	stereo3dMode.Present();

	screen->Update ();

	StatusBar->AttachToPlayer (&players[consoleplayer]); // just in case...
}


//-----------------------------------------------------------------------------
//
// renders the view - splitscreen version
//
//-----------------------------------------------------------------------------

void FGLRenderer::RenderTwoViews (player_t* player, player_t* player2)
{
	OpenGLFrameBuffer* GLTarget = static_cast<OpenGLFrameBuffer*>(screen);

	R_ResetViewInterpolation();

	gl_RenderState.SetVertexBuffer(mVBO);
	GLRenderer->mVBO->Reset();

	// reset statistics counters
	ResetProfilingData();

	// Get this before everything else
	r_TicFracF = 1.;
	gl_frameMS = I_MSTime();

	P_FindParticleSubsectors ();

	if (!gl.legacyMode) GLRenderer->mLights->Clear();

	// NoInterpolateView should have no bearing on camera textures, but needs to be preserved for the main view below.
	bool saved_niv = NoInterpolateView;
	NoInterpolateView = false;
	// prepare all camera textures that have been used in the last frame
	FCanvasTextureInfo::UpdateAll();
	NoInterpolateView = saved_niv;


	// now render the main view
	float fovratio;
	float ratio = WidescreenRatio;
	if (WidescreenRatio >= 1.3f)
	{
		fovratio = 1.333333f;
	}
	else
	{
		fovratio = ratio;
	}

	SetFixedColormap (player);

	// Check if there's some lights. If not some code can be skipped.
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	GLRenderer->mLightCount = ((it.Next()) != NULL);

	sector_t * viewsector = RenderTwoViewpoints(player->camera, player2->camera, NULL, FieldOfView.Degrees, ratio, fovratio, true, true);

	All.Unclock();
}


//-----------------------------------------------------------------------------
//
// Renders two viewpoints in a scene for splitscreen
//
//-----------------------------------------------------------------------------

sector_t * FGLRenderer::RenderTwoViewpoints (AActor * camera, AActor * camera2, GL_IRECT * bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{       
	sector_t * lviewsector;
	mSceneClearColor[0] = 0.0f;
	mSceneClearColor[1] = 0.0f;
	mSceneClearColor[2] = 0.0f;

	// Scroll the sky
	mSky1Pos = (float)fmod(gl_frameMS * level.skyspeed1, 1024.f) * 90.f/256.f;
	mSky2Pos = (float)fmod(gl_frameMS * level.skyspeed2, 1024.f) * 90.f/256.f;

	// 'viewsector' will not survive the rendering so it cannot be used anymore below.
	lviewsector = viewsector;

	// Render (potentially) multiple views for stereo 3d
	float viewShift[3];
	const s3d::Stereo3DMode& stereo3dMode = mainview && toscreen? s3d::Stereo3DMode::getCurrentMode() : s3d::Stereo3DMode::getMonoMode();
	if (camera2 != nullptr)
		stereo3dMode.SetUp();
	for (int eye_ix = 0; eye_ix < stereo3dMode.eye_count(); ++eye_ix)
	{
		if (eye_ix == 0)
			R_SetupFrame (camera);
		else
			R_SetupFrame (camera2);

		SetViewArea();

		// We have to scale the pitch to account for the pixel stretching, because the playsim doesn't know about this and treats it as 1:1.
		double radPitch = ViewPitch.Normalized180().Radians();
		double angx = cos(radPitch);
		double angy = sin(radPitch) * glset.pixelstretch;
		double alen = sqrt(angx*angx + angy*angy);

		mAngles.Pitch = (float)RAD2DEG(asin(angy / alen));
		mAngles.Roll.Degrees = ViewRoll.Degrees;

		if (camera->player && camera->player-players==consoleplayer &&
			((camera->player->cheats & CF_CHASECAM) || (r_deathcamera && camera->health <= 0)) && camera==camera->player->mo)
		{
			mViewActor=NULL;
		}
		else
		{
			mViewActor=camera;
		}

		const s3d::EyePose * eye = stereo3dMode.getEyePose(eye_ix);
		eye->SetUp();
		SetOutputViewport(bounds);
		Set3DViewport(mainview);
		mDrawingScene2D = true;
		mCurrentFoV = fov;
		// Stereo mode specific perspective projection
		SetProjection( eye->GetProjection(fov, ratio, fovratio) );
		// SetProjection(fov, ratio, fovratio);	// switch to perspective mode and set up clipper
		SetViewAngle(ViewAngle);
		// Stereo mode specific viewpoint adjustment - temporarily shifts global ViewPos
		eye->GetViewShift(GLRenderer->mAngles.Yaw.Degrees, viewShift);
		s3d::ScopedViewShifter viewShifter(viewShift);
		SetViewMatrix(ViewPos.X, ViewPos.Y, ViewPos.Z, false, false);
		gl_RenderState.ApplyMatrices();

		clipper.Clear();
		angle_t a1 = FrustumAngle();
		clipper.SafeAddClipRangeRealAngles(ViewAngle.BAMs() + a1, ViewAngle.BAMs() - a1);

		ProcessScene(toscreen);
		if (lviewsector && lviewsector->e)
		{
			if (mainview && toscreen)
				if (eye_ix == 0)
					EndDrawScene(lviewsector); // do not call this for camera textures.
				else
					EndDrawScene2(lviewsector);
		}
		if (mainview && FGLRenderBuffers::IsEnabled())
		{
			PostProcessScene();

			// This should be done after postprocessing, not before.
			mBuffers->BindCurrentFB();
			glViewport(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);

			if (!toscreen)
			{
				gl_RenderState.mViewMatrix.loadIdentity();
				gl_RenderState.mProjectionMatrix.ortho(mScreenViewport.left, mScreenViewport.width, mScreenViewport.height, mScreenViewport.top, -1.0f, 1.0f);
				gl_RenderState.ApplyMatrices();
			}
			DrawBlend(lviewsector);		
		}
		mDrawingScene2D = false;
		if (!stereo3dMode.IsMono() && FGLRenderBuffers::IsEnabled())
			mBuffers->BlitToEyeTexture(eye_ix);
		eye->TearDown();
	}
	stereo3dMode.TearDown();

	gl_frameCount++;	// This counter must be increased right before the interpolations are restored.
	interpolator.RestoreInterpolations ();
	return lviewsector;
}


//==========================================================================
//
// G_HandleSplitscreen()
//
// Sets up another player if necessary, and then processes input for that
// player. Rendering the other screen will be handled by the VR code.
//
//==========================================================================

extern int playerfornode[];

void G_CreateSplitStatusBar()
{
	if (StatusBar2 != NULL)
	{
		StatusBar2->Destroy();
		StatusBar2 = NULL;
	}
	auto cls = PClass::FindClass("DoomStatusBar");

	if (cls && gameinfo.gametype == GAME_Doom)
	{
		StatusBar2 = (DBaseStatusBar*)cls->CreateNew();
	}
	else if (SBarInfoScript[SCRIPT_CUSTOM] != NULL)
	{
		int cstype = SBarInfoScript[SCRIPT_CUSTOM]->GetGameType();

		//Did the user specify a "base"
		if(cstype == GAME_Strife)
		{
			StatusBar2 = CreateStrifeStatusBar();
		}
		else if(cstype == GAME_Any) //Use the default, empty or custom.
		{
			StatusBar2 = CreateCustomStatusBar(SCRIPT_CUSTOM);
		}
		else
		{
			StatusBar2 = CreateCustomStatusBar(SCRIPT_DEFAULT);
		}
	}
	if (StatusBar == NULL)
	{
		if (gameinfo.gametype & (GAME_DoomChex|GAME_Heretic|GAME_Hexen))
		{
			StatusBar2 = CreateCustomStatusBar (SCRIPT_DEFAULT);
		}
		else if (gameinfo.gametype == GAME_Strife)
		{
			StatusBar2 = CreateStrifeStatusBar ();
		}
		else
		{
			StatusBar2 = new DBaseStatusBar (0);
		}
	}
}

void G_HandleSplitscreen(ticcmd_t* cmd)
{
	if (netgame)
	{
		Printf("Splitscreen is not yet supported in netgames!\n");
		splitscreen = false;
		G_BuildTiccmd (cmd);
		return;
	}
	if (!GLRenderer)
	{
		Printf("Splitscreen is not yet supported in the software renderer!\n");
		splitscreen = false;
		G_BuildTiccmd (cmd);
		return;
	}
	if (demorecording)
	{
		Printf("Splitscreen is not yet supported in demos!\n");
		splitscreen = false;
		G_BuildTiccmd (cmd);
		return;
	}

	// splitscreen player not yet spawned, find a spot for them
	if (consoleplayer2 == -1)
	{
		int oldcp = consoleplayer;
		int bnum = 0;
		// Player not in game yet, let's tic like we're single player, we'll do split tics later.
		G_BuildTiccmd (cmd);

		// taken from the botcode
		for (bnum = 0; bnum < MAXPLAYERS; bnum++)
			if (!playeringame[bnum])
				break;

		if (bnum == MAXPLAYERS)
		{
			Printf ("The maximum of %d players/bots has been reached\n", MAXPLAYERS);
			splitscreen = false;
			return;
		}

		// [SP] Set up other "eye"
		//if (vr_mode == 0)
			//vr_mode = 3;

		multiplayer = true;
		playeringame[bnum] = true;
		consoleplayer2 = bnum;
		players[bnum].mo = nullptr;
		//players[bnum].userinfo.TransferFrom(players[consoleplayer].userinfo);
		players[bnum].playerstate = PST_ENTER;

		if (teamplay)
			Printf ("%s joined the %s team\n", players[bnum].userinfo.GetName(), Teams[players[bnum].userinfo.GetTeam()].GetName());
		else
			Printf ("%s joined the game\n", players[bnum].userinfo.GetName());

		G_DoReborn (bnum, true);
		if (StatusBar != NULL)
		{
			StatusBar->MultiplayerChanged ();
		}
		if (StatusBar2 == NULL)
			G_CreateSplitStatusBar();
		V_FixAspectSettings();
		return;
	}

	if (StatusBar2 == NULL)
		G_CreateSplitStatusBar();

	G_BuildTiccmd_Split (cmd, &netcmds[consoleplayer2][maketic%BACKUPTICS]);
}

//==========================================================================
//
// G_DestroySplitscreen()
//
// If we have a splitscreen player, remove them from the game!
//
//==========================================================================

void G_DestroySplitscreen()
{
	if (consoleplayer2 == -1)
		return;

	G_DoPlayerPop(consoleplayer2);

	if (StatusBar2 != NULL)
	{
		StatusBar2->Destroy();
		StatusBar2 = NULL;
	}

	V_FixAspectSettings();

	//vr_mode = 0;
	consoleplayer2 = -1;
}


//==========================================================================
//
// G_BuildTiccmd_Split
//
// [SP] This is the split-screen version of G_BuildTiccmd - this discriminates by input.
// For now, only Gamepad input is split.
//
//==========================================================================

void G_BuildTiccmd_Split (ticcmd_t *cmd, ticcmd_t *cmd2)
{
	int 		strafe;
	int 		speed;
	int 		forward;
	int 		side;
	int			fly;

	//int 		strafe2;
	//int 		speed2;
	int 		forward2;
	int 		side2;
	int			fly2;

	ticcmd_t	*base;

	base = I_BaseTiccmd (); 			// empty, or external driver
	*cmd = *base;
	*cmd2 = *base;

	cmd->consistancy = consistancy[consoleplayer][(maketic/ticdup)%BACKUPTICS];
	cmd2->consistancy = consistancy[consoleplayer2][(maketic/ticdup)%BACKUPTICS];

	strafe = Button_Strafe.bDown;
	speed = Button_Speed.bDown ^ (int)cl_run;

	forward = side = fly = 0;
	forward2 = side2 = fly2 = 0;

	// [RH] only use two stage accelerative turning on the keyboard
	//		and not the joystick, since we treat the joystick as
	//		the analog device it is.
	if (Button_Left.bDown || Button_Right.bDown)
		turnheld += ticdup;
	else
		turnheld = 0;

	// let movement keys cancel each other out
	if (strafe)
	{
		if (Button_Right.bDown)
			side += sidemove[speed];
		if (Button_Left.bDown)
			side -= sidemove[speed];
	}
	else
	{
		int tspeed = speed;

		if (turnheld < SLOWTURNTICS)
			tspeed += 2;		// slow turn
		
		if (Button_Right.bDown)
		{
			G_AddViewAngle (angleturn[tspeed]);
			LocalKeyboardTurner = true;
		}
		if (Button_Left.bDown)
		{
			G_AddViewAngle (-angleturn[tspeed]);
			LocalKeyboardTurner = true;
		}
	}

	if (Button_LookUp.bDown)
	{
		G_AddViewPitch (lookspeed[speed]);
		LocalKeyboardTurner = true;
	}
	if (Button_LookDown.bDown)
	{
		G_AddViewPitch (-lookspeed[speed]);
		LocalKeyboardTurner = true;
	}

	if (Button_MoveUp.bDown)
		fly += flyspeed[speed];
	if (Button_MoveDown.bDown)
		fly -= flyspeed[speed];

	if (Button_Klook.bDown)
	{
		if (Button_Forward.bDown)
			G_AddViewPitch (lookspeed[speed]);
		if (Button_Back.bDown)
			G_AddViewPitch (-lookspeed[speed]);
	}
	else
	{
		if (Button_Forward.bDown)
			forward += forwardmove[speed];
		if (Button_Back.bDown)
			forward -= forwardmove[speed];
	}

	if (Button_MoveRight.bDown)
		side += sidemove[speed];
	if (Button_MoveLeft.bDown)
		side -= sidemove[speed];

	// buttons
	if (Button_Attack.bDown)		cmd->ucmd.buttons |= BT_ATTACK;
	if (Button_AltAttack.bDown)		cmd->ucmd.buttons |= BT_ALTATTACK;
	if (Button_Use.bDown)			cmd->ucmd.buttons |= BT_USE;
	if (Button_Jump.bDown)			cmd->ucmd.buttons |= BT_JUMP;
	if (Button_Crouch.bDown)		cmd->ucmd.buttons |= BT_CROUCH;
	if (Button_Zoom.bDown)			cmd->ucmd.buttons |= BT_ZOOM;
	if (Button_Reload.bDown)		cmd->ucmd.buttons |= BT_RELOAD;

	if (Button_User1.bDown)			cmd->ucmd.buttons |= BT_USER1;
	if (Button_User2.bDown)			cmd->ucmd.buttons |= BT_USER2;
	if (Button_User3.bDown)			cmd->ucmd.buttons |= BT_USER3;
	if (Button_User4.bDown)			cmd->ucmd.buttons |= BT_USER4;

	if (Button_Speed.bDown)			cmd->ucmd.buttons |= BT_SPEED;
	if (Button_Strafe.bDown)		cmd->ucmd.buttons |= BT_STRAFE;
	if (Button_MoveRight.bDown)		cmd->ucmd.buttons |= BT_MOVERIGHT;
	if (Button_MoveLeft.bDown)		cmd->ucmd.buttons |= BT_MOVELEFT;
	if (Button_LookDown.bDown)		cmd->ucmd.buttons |= BT_LOOKDOWN;
	if (Button_LookUp.bDown)		cmd->ucmd.buttons |= BT_LOOKUP;
	if (Button_Back.bDown)			cmd->ucmd.buttons |= BT_BACK;
	if (Button_Forward.bDown)		cmd->ucmd.buttons |= BT_FORWARD;
	if (Button_Right.bDown)			cmd->ucmd.buttons |= BT_RIGHT;
	if (Button_Left.bDown)			cmd->ucmd.buttons |= BT_LEFT;
	if (Button_MoveDown.bDown)		cmd->ucmd.buttons |= BT_MOVEDOWN;
	if (Button_MoveUp.bDown)		cmd->ucmd.buttons |= BT_MOVEUP;
	if (Button_ShowScores.bDown)	cmd->ucmd.buttons |= BT_SHOWSCORES;

	// Handle joysticks/game controllers.
	int consoleplayer1 = consoleplayer;

	consoleplayer = consoleplayer2;
	float joyaxes[NUM_JOYAXIS];

	I_GetAxes(joyaxes);

	// Remap some axes depending on button state.
	if (Button_Strafe.bDown || (Button_Mlook.bDown && lookstrafe))
	{
		joyaxes[JOYAXIS_Side] = joyaxes[JOYAXIS_Yaw];
		joyaxes[JOYAXIS_Yaw] = 0;
	}
	if (Button_Mlook.bDown)
	{
		joyaxes[JOYAXIS_Pitch] = joyaxes[JOYAXIS_Forward];
		joyaxes[JOYAXIS_Forward] = 0;
	}

	if (joyaxes[JOYAXIS_Pitch] != 0)
	{
		G_AddViewPitch(joyint(joyaxes[JOYAXIS_Pitch] * 2048));
		LocalKeyboardTurner = true;
	}
	if (joyaxes[JOYAXIS_Yaw] != 0)
	{
		G_AddViewAngle(joyint(-1280 * joyaxes[JOYAXIS_Yaw]));
		LocalKeyboardTurner = true;
	}
	side2 -= joyint(sidemove[speed] * joyaxes[JOYAXIS_Side]);
	forward2 += joyint(joyaxes[JOYAXIS_Forward] * forwardmove[speed]);
	fly2 += joyint(joyaxes[JOYAXIS_Up] * 2048);

	consoleplayer = consoleplayer1;

	// Handle mice.
	if (!Button_Mlook.bDown && !freelook)
	{
		forward += (int)((float)mousey * m_forward);
	}

	cmd->ucmd.pitch = LocalViewPitch >> 16;

	if (SendLand)
	{
		SendLand = false;
		fly = -32768;
	}

	if (strafe || lookstrafe)
		side += (int)((float)mousex * m_side);

	mousex = mousey = 0;

	// Build command.
	if (forward > MAXPLMOVE)
		forward = MAXPLMOVE;
	else if (forward < -MAXPLMOVE)
		forward = -MAXPLMOVE;
	if (side > MAXPLMOVE)
		side = MAXPLMOVE;
	else if (side < -MAXPLMOVE)
		side = -MAXPLMOVE;

	cmd->ucmd.forwardmove += forward;
	cmd->ucmd.sidemove += side;
	cmd->ucmd.yaw = LocalViewAngle >> 16;
	cmd->ucmd.upmove = fly;
	LocalViewAngle = 0;
	LocalViewPitch = 0;

	// and for the gamepad...
	if (forward2 > MAXPLMOVE)
		forward2 = MAXPLMOVE;
	else if (forward2 < -MAXPLMOVE)
		forward2 = -MAXPLMOVE;
	if (side2 > MAXPLMOVE)
		side2 = MAXPLMOVE;
	else if (side2 < -MAXPLMOVE)
		side2 = -MAXPLMOVE;
	cmd2->ucmd.forwardmove += forward2;
	cmd2->ucmd.sidemove += side2;
	cmd2->ucmd.yaw = LocalViewAngle >> 16;
	cmd2->ucmd.upmove = fly2;

	// special buttons
	if (sendturn180)
	{
		sendturn180 = false;
		cmd->ucmd.buttons |= BT_TURN180;
	}
	if (sendpause)
	{
		sendpause = false;
		Net_WriteByte (DEM_PAUSE);
	}
	if (sendsave)
	{
		sendsave = false;
		Net_WriteByte (DEM_SAVEGAME);
		Net_WriteString (savegamefile);
		Net_WriteString (savedescription);
		savegamefile = "";
	}
	if (SendItemUse == (const AInventory *)1)
	{
		Net_WriteByte (DEM_INVUSEALL);
		SendItemUse = NULL;
	}
	else if (SendItemUse != NULL)
	{
		Net_WriteByte (DEM_INVUSE);
		Net_WriteLong (SendItemUse->InventoryID);
		SendItemUse = NULL;
	}
	if (SendItemDrop != NULL)
	{
		Net_WriteByte (DEM_INVDROP);
		Net_WriteLong (SendItemDrop->InventoryID);
		SendItemDrop = NULL;
	}

	cmd->ucmd.forwardmove <<= 8;
	cmd->ucmd.sidemove <<= 8;
	cmd2->ucmd.forwardmove <<= 8;
	cmd2->ucmd.sidemove <<= 8;
}


//==========================================================================
//
// "splitswap" CCMD
//
// [SP] Swaps the split screen players so player 2 is now the keyboard input.
//
//==========================================================================


CCMD (splitswap)
{
	int old_consoleplayer = consoleplayer;
	if (!splitscreen)
	{
		Printf("Splitscreen is not active!\n");
		return;
	}
	if (consoleplayer2 == -1)
	{
		Printf("Splitscreen player is not yet spawned!\n");
		return;
	}
	consoleplayer = playerfornode[0] = Net_Arbitrator = consoleplayer2;
	consoleplayer2 = old_consoleplayer;
}

