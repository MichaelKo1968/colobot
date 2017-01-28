// D3DApp.cpp

#define STRICT
#define D3D_OVERLOADS

#include <windows.h>
//#include <pbt.h>
#include <mmsystem.h>
#include <stdio.h>
#include <direct.h>
#include <tchar.h>
#include <zmouse.h>
#include <dinput.h>

#include "struct.h"
#include "D3DTextr.h"
#include "D3DEngine.h"
#include "language.h"
#include "event.h"
#include "profile.h"
#include "iman.h"
#include "restext.h"
#include "math3d.h"
#include "joystick.h"
#include "robotmain.h"
#include "sound.h"
#include "D3DApp.h"




#define AUDIO_TRACK		13			// nb total de pistes audio sur le CD
#define MAX_STEP		0.2f		// temps maximum pour un step

#define WINDOW_DX		(640+6)		// dimensions en mode fen�tr�
#define WINDOW_DY		(480+25)

#define USE_THREAD		FALSE		// TRUE ne fonctionne pas !
#define TIME_THREAD		0.02f




// Limite le d�battement des commandes clavier & joystick.

float AxeLimit(float value)
{
	if ( value < -1.0f )  value = -1.0f;
	if ( value >  1.0f )  value =  1.0f;
	return value;
}


// Entry point to the program. Initializes everything, and goes into a
// message-processing loop. Idle time is used to render the scene.

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR strCmdLine, INT )
{
	Error	err;
	char	string[100];

	CD3DApplication d3dApp;  // unique instance de l'application

	err = d3dApp.CheckMistery(strCmdLine);
	if ( err != ERR_OK )
	{
		GetResource(RES_ERR, err, string);
		MessageBox( NULL, string, _T("BuzzingCars"), MB_ICONERROR|MB_OK );
		return 0;
	}

	if ( FAILED(d3dApp.Create(hInst, strCmdLine)) )
	{
		return 0;
	}

	return d3dApp.Run();  // ex�cution du tout
}


// Internal function prototypes and variables.

enum APPMSGTYPE { MSG_NONE, MSGERR_APPMUSTEXIT, MSGWARN_SWITCHEDTOSOFTWARE };

static INT     CALLBACK AboutProc( HWND, UINT, WPARAM, LPARAM );
static LRESULT CALLBACK WndProc( HWND, UINT, WPARAM, LPARAM );

static CD3DApplication* g_pD3DApp;



// Constructor.

CD3DApplication::CD3DApplication()
{
	int		i;

	m_iMan = new(CInstanceManager);
	m_event = new CEvent(m_iMan);

	m_pD3DEngine = 0;
	m_pRobotMain = 0;
	m_pSound     = 0;
	m_pFramework = 0;
	m_instance   = 0;
	m_hWnd       = 0;
	m_pDD        = 0;
	m_pD3D       = 0;
	m_pD3DDevice = 0;

	m_CDpath[0] = 0;

	m_pddsRenderTarget = 0;
	m_pddsDepthBuffer  = 0;

	m_keyState = 0;
	m_axeKeyX = 0.0f;
	m_axeKeyY = 0.0f;
	m_axeKeyZ = 0.0f;
	m_axeKeyW = 0.0f;
	m_axeJoy = D3DVECTOR(0.0f, 0.0f, 0.0f);

	m_vidMemTotal  = 0;
	m_bActive      = FALSE;
	m_bActivateApp = FALSE;
	m_bReady       = FALSE;
	m_joystick     = 0;  // clavier
	m_FFBforce     = 1.0f;
	m_bFFB         = FALSE;
	m_aTime        = 0.0f;

	for ( i=0 ; i<32 ; i++ )
	{
		m_bJoyButton[i] = FALSE;
	}

	m_bJoyLeft  = FALSE;
	m_bJoyRight = FALSE;
	m_bJoyUp    = FALSE;
	m_bJoyDown  = FALSE;

	m_strWindowTitle  = _T("BuzzingCars");
	m_bAppUseZBuffer  = TRUE;
	m_bAppUseStereo   = TRUE;
	m_bShowStats      = FALSE;
	m_bDebugMode      = FALSE;
	m_bAudioState     = TRUE;
	m_bAudioTrack     = TRUE;
	m_bNiceMouse      = FALSE;
	m_fnConfirmDevice = 0;

	ResetKey();

	g_pD3DApp = this;

	// Demande l'�v�nement envoy� par les souris Logitech.
	m_mshMouseWheel = RegisterWindowMessage(MSH_MOUSEWHEEL); 

	_mkdir("files\\");
}


// Destructor.

CD3DApplication::~CD3DApplication()
{
	delete m_iMan;
}



// Retourne le chemin d'acc�s du CD.

char* CD3DApplication::RetCDpath()
{
	return m_CDpath;
}

// Lit les informations dans la base de registre.

Error CD3DApplication::RegQuery()
{
	FILE*	file = NULL;
	HKEY	key;
	LONG	i;
	DWORD	type, len;
	char	filename[100];

	i = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Epsitec\\BuzzingCars\\Setup",
					 0, KEY_READ, &key);
	if ( i != ERROR_SUCCESS )  return ERR_INSTALL;

	type = REG_SZ;
	len  = sizeof(m_CDpath);
	i = RegQueryValueEx(key, "CDpath", NULL, &type, (LPBYTE)m_CDpath, &len);
	if ( i != ERROR_SUCCESS || type != REG_SZ )  return ERR_INSTALL;

	filename[0] = m_CDpath[0];
	filename[1] = ':';
	filename[2] = '\\';
	filename[3] = 0;
	i = GetDriveType(filename);
	if ( i != DRIVE_CDROM )  return ERR_NOCD;

	strcat(filename, "autorun.inf");
	file = fopen(filename, "rb");  // fichier install.ini inexistant ?
	if ( file == NULL )  return ERR_NOCD;
	fclose(file);

	return ERR_OK;
}

// V�rifie la pr�sence des pistes audio sur le CD.

Error CD3DApplication::AudioQuery()
{
	MCI_OPEN_PARMS		mciOpenParms;
	MCI_STATUS_PARMS	mciStatusParms;
	DWORD				dwReturn;
	UINT				deviceID;
	char				device[10];

	// Open the device by specifying the device and filename.
	// MCI will attempt to choose the MIDI mapper as the output port.
	memset(&mciOpenParms, 0, sizeof(MCI_OPEN_PARMS));
	mciOpenParms.lpstrDeviceType = (LPCTSTR)MCI_DEVTYPE_CD_AUDIO;
	if ( m_CDpath[0] == 0 )
	{
		dwReturn = mciSendCommand(NULL,
								  MCI_OPEN,
								  MCI_OPEN_TYPE|MCI_OPEN_TYPE_ID,
								  (DWORD)(LPVOID)&mciOpenParms);
	}
	else
	{
		device[0] = m_CDpath[0];
		device[1] = ':';
		device[2] = 0;
		mciOpenParms.lpstrElementName = device;
		dwReturn = mciSendCommand(NULL,
								  MCI_OPEN,
								  MCI_OPEN_TYPE|MCI_OPEN_TYPE_ID|MCI_OPEN_ELEMENT,
								  (DWORD)(LPVOID)&mciOpenParms);
	}
	if ( dwReturn != 0 )
	{
		return ERR_NOCD;
	}

	// The device opened successfully; get the device ID.
	deviceID = mciOpenParms.wDeviceID;

	memset(&mciStatusParms, 0, sizeof(MCI_STATUS_PARMS));
	mciStatusParms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	dwReturn = mciSendCommand(deviceID,
							  MCI_STATUS,
							  MCI_WAIT|MCI_STATUS_ITEM,
							  (DWORD)&mciStatusParms);
	if ( dwReturn != 0 )
	{
		mciSendCommand(deviceID, MCI_CLOSE, 0, NULL);
		return ERR_NOCD;
	}

	if ( mciStatusParms.dwReturn != AUDIO_TRACK )
	{
		mciSendCommand(deviceID, MCI_CLOSE, 0, NULL);
		return ERR_NOCD;
	}

	mciSendCommand(deviceID, MCI_CLOSE, 0, NULL);
	return ERR_OK;
}

// V�rifie la pr�sence de la cl�.

Error CD3DApplication::CheckMistery(char *strCmdLine)
{
	if ( strstr(strCmdLine, "-debug") != 0 )
	{
		m_bShowStats = TRUE;
		SetDebugMode(TRUE);
	}

	if ( strstr(strCmdLine, "-audiostate") != 0 )
	{
		m_bAudioState = FALSE;
	}

	if ( strstr(strCmdLine, "-audiotrack") != 0 )
	{
		m_bAudioTrack = FALSE;
	}

	m_CDpath[0] = 0;
#if _FULL & !_EGAMES
	if ( strstr(strCmdLine, "-nocd") == 0 && !m_bDebugMode )
	{
		Error	err;

		err = RegQuery();
		if ( err != ERR_OK )  return err;

//?		err = AudioQuery();
//?		if ( err != ERR_OK )  return err;
	}
#else
	m_bAudioTrack = FALSE;
#endif

	return ERR_OK;
}


// Retourne la quantit� totale de m�moire vid�o pour les textures.

int CD3DApplication::GetVidMemTotal()
{
	return m_vidMemTotal;
}

BOOL CD3DApplication::IsVideo8MB()
{
	if ( m_vidMemTotal == 0 )  return FALSE;
	return (m_vidMemTotal <= 8388608L);  // 8 Mb ou moins (2^23) ?
}

BOOL CD3DApplication::IsVideo32MB()
{
	if ( m_vidMemTotal == 0 )  return FALSE;
	return (m_vidMemTotal > 16777216L);  // plus de 16 Mb (2^24) ?
}


void CD3DApplication::SetShowStat(BOOL bShow)
{
	m_bShowStats = bShow;
}

BOOL CD3DApplication::RetShowStat()
{
	return m_bShowStats;
}


void CD3DApplication::SetDebugMode(BOOL bMode)
{
	m_bDebugMode = bMode;
	D3DTextr_SetDebugMode(m_bDebugMode);
}

BOOL CD3DApplication::RetDebugMode()
{
	return m_bDebugMode;
}




// Processus fils de gestion du temps.

DWORD WINAPI ThreadRoutine(LPVOID)
{
	Event	event;
	float	time;
	int		ms, start, end, delay;

	ms = (int)(TIME_THREAD*1000.0f);
	time = 0.0f;
	while ( TRUE )
	{
		start = timeGetTime();

		g_pD3DApp->m_pD3DEngine->FrameMove(TIME_THREAD);

		ZeroMemory(&event, sizeof(Event));
		event.event = EVENT_FRAME;
		event.rTime = TIME_THREAD;
		event.axeX = AxeLimit(g_pD3DApp->m_axeKeyX + g_pD3DApp->m_axeJoy.x);
		event.axeY = AxeLimit(g_pD3DApp->m_axeKeyY + g_pD3DApp->m_axeJoy.y);
		event.axeZ = AxeLimit(g_pD3DApp->m_axeKeyZ + g_pD3DApp->m_axeJoy.z);
		event.axeW = g_pD3DApp->m_axeKeyW;
		event.keyState = g_pD3DApp->m_keyState;

		if ( g_pD3DApp->m_pRobotMain != 0 )
		{
			g_pD3DApp->m_pRobotMain->EventProcess(event);
		}

		end = timeGetTime();

		delay = ms-(end-start);
		if ( delay > 0 )
		{
			Sleep(delay);  // attend 20ms-used
		}
		time += TIME_THREAD;
	}
	return 0;
}


// Called during device intialization, this code checks the device
// for some minimum set of capabilities.

HRESULT CD3DApplication::ConfirmDevice( DDCAPS* pddDriverCaps,
									    D3DDEVICEDESC7* pd3dDeviceDesc )
{
//?	if( pd3dDeviceDesc->wMaxVertexBlendMatrices < 2 )
//?		return E_FAIL;

    return S_OK;
}

// Create the application.

HRESULT CD3DApplication::Create( HINSTANCE hInst, TCHAR* strCmdLine )
{
	HRESULT hr;
	char	deviceName[100];
	char	modeName[100];
	int		iValue;
	float	fValue;
	DWORD	style;
	BOOL	bFull, b3D;

	m_instance = hInst;

	InitCurrentDirectory();

	// Enumerate available D3D devices. The callback is used so the app can
	// confirm/reject each enumerated device depending on its capabilities.
	if( FAILED( hr = D3DEnum_EnumerateDevices( m_fnConfirmDevice ) ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		return hr;
	}

	if( FAILED( hr = D3DEnum_SelectDefaultDevice( &m_pDeviceInfo ) ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		return hr;
	}

	if ( !m_bDebugMode )
	{
		m_pDeviceInfo->bWindowed = FALSE;  // plein �cran
	}
	if ( GetProfileInt("Device", "FullScreen", bFull) )
	{
		m_pDeviceInfo->bWindowed = !bFull;
	}

	// Create the 3D engine.
	if( (m_pD3DEngine = new CD3DEngine(m_iMan, this)) == NULL )
	{
		DisplayFrameworkError( D3DENUMERR_ENGINE, MSGERR_APPMUSTEXIT );
		return E_OUTOFMEMORY;
	}
	SetEngine(m_pD3DEngine);

	// Initialize the app's custom scene stuff
	if( FAILED( hr = m_pD3DEngine->OneTimeSceneInit() ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		return hr;
	}

	// Create a new CD3DFramework class. This class does all of our D3D
	// initialization and manages the common D3D objects.
	if( (m_pFramework = new CD3DFramework7()) == NULL )
	{
		DisplayFrameworkError( E_OUTOFMEMORY, MSGERR_APPMUSTEXIT );
		return E_OUTOFMEMORY;
	}

	// Create the sound instance.
	if( (m_pSound = new CSound(m_iMan)) == NULL )
	{
		DisplayFrameworkError( D3DENUMERR_SOUND, MSGERR_APPMUSTEXIT );
		return E_OUTOFMEMORY;
	}

	// Create the robot application.
	if( (m_pRobotMain = new CRobotMain(m_iMan)) == NULL )
	{
		DisplayFrameworkError( D3DENUMERR_ROBOT, MSGERR_APPMUSTEXIT );
		return E_OUTOFMEMORY;
	}

	// Register the window class
	WNDCLASS wndClass = { 0, WndProc, 0, 0, hInst,
						  LoadIcon( hInst, MAKEINTRESOURCE(IDI_MAIN_ICON) ),
						  LoadCursor( NULL, IDC_ARROW ), 
						  (HBRUSH)GetStockObject(WHITE_BRUSH),
						  NULL, _T("D3D Window") };
	RegisterClass( &wndClass );

	// Create the render window
	style = WS_CAPTION|WS_VISIBLE;
	if ( m_bDebugMode )  style |= WS_SYSMENU;  // case de fermeture
	m_hWnd = CreateWindow( _T("D3D Window"), m_strWindowTitle,
//?						   WS_OVERLAPPEDWINDOW|WS_VISIBLE,
						   style, CW_USEDEFAULT, CW_USEDEFAULT,
						   WINDOW_DX, WINDOW_DY, 0L,
//?						   LoadMenu( hInst, MAKEINTRESOURCE(IDR_MENU) ), 
						   NULL,
						   hInst, 0L );
	UpdateWindow( m_hWnd );

	if ( !GetProfileInt("Setup", "Sound3D", b3D) )
	{
		b3D = TRUE;
	}
	m_pSound->SetDebugMode(m_bDebugMode);
	m_pSound->Create(m_hWnd, b3D);
	m_pSound->CacheAll();
	m_pSound->SetState(m_bAudioState);
	m_pSound->SetAudioTrack(m_bAudioTrack);
	m_pSound->SetCDpath(m_CDpath);

	// Initialize the 3D environment for the app
	if( FAILED( hr = Initialize3DEnvironment() ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		Cleanup3DEnvironment();
		return E_FAIL;
	}

	// Change the display device driver.
	GetProfileString("Device", "Name", deviceName, 100);
	GetProfileString("Device", "Mode", modeName, 100);
	GetProfileInt("Device", "FullScreen", bFull);
	if ( deviceName[0] != 0 && modeName[0] != 0 && bFull )
	{
		ChangeDevice(deviceName, modeName, bFull);
	}

	// Premi�re ex�cution ?
	if ( !GetProfileInt("Setup", "ObjectDirty", iValue) )
	{
		m_pD3DEngine->FirstExecuteAdapt(TRUE);
	}

	// Utilise un joystick ?
	if ( GetProfileFloat("Setup", "JoystickForce", fValue) )
	{
		m_pD3DEngine->SetForce(fValue);
	}
	if ( GetProfileInt("Setup", "JoystickFFB", iValue) )
	{
		m_pD3DEngine->SetFFB(iValue);
	}
	if ( GetProfileInt("Setup", "UseJoystick", iValue) )
	{
		m_pD3DEngine->SetJoystick(iValue);
	}

	// Cr�e le fichier buzzingcars.ini � la premi�re ex�cution.
	m_pRobotMain->CreateIni();

	m_pRobotMain->ChangePhase(PHASE_WELCOME3);
	m_pD3DEngine->TimeInit();

#if USE_THREAD
	m_thread = CreateThread(NULL, 0, ThreadRoutine, this, 0, &m_threadId);
	SetThreadPriority(m_thread, THREAD_PRIORITY_ABOVE_NORMAL);
#endif

	// The app is ready to go
	m_bReady = TRUE;

	return S_OK;
}


// Message-processing loop. Idle time is used to render the scene.

INT CD3DApplication::Run()
{
	// Load keyboard accelerators
	HACCEL hAccel = LoadAccelerators( NULL, MAKEINTRESOURCE(IDR_MAIN_ACCEL) );

	// Now we're ready to recieve and process Windows messages.
	BOOL bGotMsg;
	MSG  msg;
	PeekMessage( &msg, NULL, 0U, 0U, PM_NOREMOVE );

	while( WM_QUIT != msg.message  )
	{
		// Use PeekMessage() if the app is active, so we can use idle time to
		// render the scene. Else, use GetMessage() to avoid eating CPU time.
		if( m_bActive )
			bGotMsg = PeekMessage( &msg, NULL, 0U, 0U, PM_REMOVE );
		else
			bGotMsg = GetMessage( &msg, NULL, 0U, 0U );

		if( bGotMsg )
		{
			// Translate and dispatch the message
			if( TranslateAccelerator( m_hWnd, hAccel, &msg ) == 0 )
			{
				TranslateMessage( &msg );
				DispatchMessage( &msg );
			}
		}
		else
		{
			// Render a frame during idle time (no messages are waiting)
			if( m_bActive && m_bReady )
			{
				Event	event;

				while ( m_event->GetEvent(event) )
				{
					if ( event.event == EVENT_QUIT )
					{
//? 					SendMessage( m_hWnd, WM_CLOSE, 0, 0 );
						m_pSound->StopMusic();
						Cleanup3DEnvironment();
						PostQuitMessage(0);
						return msg.wParam;
					}
					m_pRobotMain->EventProcess(event);
				}

				if ( !RetNiceMouse() )
				{
					SetMouseType(m_pD3DEngine->RetMouseType());
				}

				if( FAILED( Render3DEnvironment() ) )
					DestroyWindow( m_hWnd );
			}
		}
	}

	return msg.wParam;
}



// Conversion de la position de la souris.
// x: 0=gauche, 1=droite
// y: 0=bas, 1=haut

FPOINT CD3DApplication::ConvPosToInterface(HWND hWnd, LPARAM lParam)
{
	POINT	cpos;
	FPOINT	pos;
	float	px, py, w, h;

	cpos.x = (short)LOWORD(lParam);
	cpos.y = (short)HIWORD(lParam);

	if ( !m_pDeviceInfo->bWindowed )
	{
		ClientToScreen(hWnd, &cpos);
	}

	px = (float)cpos.x;
	py = (float)cpos.y;
	w  = (float)m_ddsdRenderTarget.dwWidth;
	h  = (float)m_ddsdRenderTarget.dwHeight;

	pos.x = px/w;
	pos.y = 1.0f-py/h;

	return pos;
}

// D�place physiquement la souris.

void CD3DApplication::SetMousePos(FPOINT pos)
{
	POINT	p;

	pos.y = 1.0f-pos.y;

	pos.x *= m_ddsdRenderTarget.dwWidth;
	pos.y *= m_ddsdRenderTarget.dwHeight;

	p.x = (int)pos.x;
	p.y = (int)pos.y;
	ClientToScreen(m_hWnd, &p);
	
	SetCursorPos(p.x, p.y);
}

// Choix du type de curseur pour la souris.

void CD3DApplication::SetMouseType(D3DMouse type)
{
	HCURSOR		hc;

	if ( type == D3DMOUSEHAND )
	{
		hc = LoadCursor(m_instance, MAKEINTRESOURCE(IDC_CURSORHAND));
	}
	else if ( type == D3DMOUSEEDIT )
	{
		hc = LoadCursor(NULL, IDC_IBEAM);
	}
	else if ( type == D3DMOUSEWAIT )
	{
		hc = LoadCursor(NULL, IDC_WAIT);
	}
	else
	{
		hc = LoadCursor(NULL, IDC_ARROW);
	}

	if ( hc != NULL )
	{
		SetCursor(hc);
	}
}

// Choix du mode pour la souris.

void CD3DApplication::SetNiceMouse(BOOL bNice)
{
	if ( bNice == m_bNiceMouse )  return;
	m_bNiceMouse = bNice;

	if ( m_bNiceMouse )
	{
		ShowCursor(FALSE);  // cache la vilaine souris windows
		SetCursor(NULL);
	}
	else
	{
		ShowCursor(TRUE);  // montre la vilaine souris windows
		SetCursor(LoadCursor(NULL, IDC_ARROW));
	}
}

// Indique s'il faut utiliser la jolie souris ombr�e.

BOOL CD3DApplication::RetNiceMouse()
{
	if (  m_pDeviceInfo->bWindowed )  return FALSE;
	if ( !m_pDeviceInfo->bHardware )  return FALSE;

	return m_bNiceMouse;
}

// Indique s'il est possible d'utiliser la jolie souris ombr�e.

BOOL CD3DApplication::RetNiceMouseCap()
{
	if (  m_pDeviceInfo->bWindowed )  return FALSE;
	if ( !m_pDeviceInfo->bHardware )  return FALSE;

	return TRUE;
}


// Static msg handler which passes messages to the application class.

LRESULT CALLBACK WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if ( g_pD3DApp != 0 )
	{
		Event	event;
		short	move;

		ZeroMemory(&event, sizeof(Event));

//?		if ( uMsg  != 275 )
//?		{
//?			char s[100];
//?			sprintf(s, "event: %d %d %d\n", uMsg, wParam, lParam);
//?			OutputDebugString(s);
//?		}

		if ( uMsg == WM_LBUTTONDOWN )  event.event = EVENT_LBUTTONDOWN;
		if ( uMsg == WM_RBUTTONDOWN )  event.event = EVENT_RBUTTONDOWN;
		if ( uMsg == WM_LBUTTONUP   )  event.event = EVENT_LBUTTONUP;
		if ( uMsg == WM_RBUTTONUP   )  event.event = EVENT_RBUTTONUP;
		if ( uMsg == WM_MOUSEMOVE   )  event.event = EVENT_MOUSEMOVE;
		if ( uMsg == WM_KEYDOWN     )  event.event = EVENT_KEYDOWN;
		if ( uMsg == WM_KEYUP       )  event.event = EVENT_KEYUP;
		if ( uMsg == WM_CHAR        )  event.event = EVENT_CHAR;

		event.param = wParam;
		event.axeX = AxeLimit(g_pD3DApp->m_axeKeyX + g_pD3DApp->m_axeJoy.x);
		event.axeY = AxeLimit(g_pD3DApp->m_axeKeyY + g_pD3DApp->m_axeJoy.y);
		event.axeZ = AxeLimit(g_pD3DApp->m_axeKeyZ + g_pD3DApp->m_axeJoy.z);
		event.axeW = g_pD3DApp->m_axeKeyW;
		event.keyState = g_pD3DApp->m_keyState;

		if ( uMsg == WM_LBUTTONDOWN ||
			 uMsg == WM_RBUTTONDOWN ||
			 uMsg == WM_LBUTTONUP   ||
			 uMsg == WM_RBUTTONUP   ||
			 uMsg == WM_MOUSEMOVE   )  // �v�nement souris ?
		{
			event.pos = g_pD3DApp->ConvPosToInterface(hWnd, lParam);
			g_pD3DApp->m_mousePos = event.pos;
			g_pD3DApp->m_pD3DEngine->SetMousePos(event.pos);
		}

		if ( uMsg == WM_MOUSEWHEEL )  // molette souris ?
		{
			event.event = EVENT_KEYDOWN;
			event.pos = g_pD3DApp->m_mousePos;
			move = HIWORD(wParam);
			if ( move/WHEEL_DELTA > 0 )  event.param = VK_WHEELUP;
			if ( move/WHEEL_DELTA < 0 )  event.param = VK_WHEELDOWN;
		}
		if ( g_pD3DApp->m_mshMouseWheel != 0 &&
			 uMsg == g_pD3DApp->m_mshMouseWheel )  // molette souris Logitech ?
		{
			event.event = EVENT_KEYDOWN;
			event.pos = g_pD3DApp->m_mousePos;
			move = LOWORD(wParam);
			if ( move/WHEEL_DELTA > 0 )  event.param = VK_WHEELUP;
			if ( move/WHEEL_DELTA < 0 )  event.param = VK_WHEELDOWN;
		}

		if ( event.event == EVENT_KEYDOWN ||
			 event.event == EVENT_KEYUP   ||
			 event.event == EVENT_CHAR    )
		{
			if ( event.param == 0 )
			{
				event.event = EVENT_NULL;
			}
		}

		if ( g_pD3DApp->m_pRobotMain != 0 && event.event != 0 )
		{
			g_pD3DApp->m_pRobotMain->EventProcess(event);
//?			if ( !g_pD3DApp->RetNiceMouse() )
//?			{
//?				g_pD3DApp->SetMouseType(g_pD3DApp->m_pD3DEngine->RetMouseType());
//?			}
		}
		if ( g_pD3DApp->m_pD3DEngine != 0 )
		{
			g_pD3DApp->m_pD3DEngine->MsgProc( hWnd, uMsg, wParam, lParam );
		}
		return g_pD3DApp->MsgProc( hWnd, uMsg, wParam, lParam );
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}


// Minimal message proc function for the about box.

BOOL CALLBACK AboutProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM )
{
    if( WM_COMMAND == uMsg )
        if( IDOK == LOWORD(wParam) || IDCANCEL == LOWORD(wParam) )
            EndDialog( hWnd, TRUE );

    return WM_INITDIALOG == uMsg ? TRUE : FALSE;
}



// Ignore les touches press�es.

void CD3DApplication::FlushPressKey()
{
	m_keyState = 0;
	m_axeKeyX = 0.0f;
	m_axeKeyY = 0.0f;
	m_axeKeyZ = 0.0f;
	m_axeKeyW = 0.0f;
	m_axeJoy = D3DVECTOR(0.0f, 0.0f, 0.0f);
}

// Remet les touches par d�faut.

void CD3DApplication::ResetKey()
{
	int		i;

	for ( i=0 ; i<50 ; i++ )
	{
		m_key[i][0] = 0;
		m_key[i][1] = 0;
	}
	m_key[KEYRANK_LEFT   ][0] = VK_LEFT;
	m_key[KEYRANK_RIGHT  ][0] = VK_RIGHT;
	m_key[KEYRANK_UP     ][0] = VK_UP;
	m_key[KEYRANK_DOWN   ][0] = VK_DOWN;
	m_key[KEYRANK_BRAKE  ][0] = VK_SPACE;
	m_key[KEYRANK_BRAKE  ][1] = VK_BUTTON1;
	m_key[KEYRANK_HORN   ][0] = VK_RETURN;
	m_key[KEYRANK_HORN   ][1] = VK_BUTTON2;
	m_key[KEYRANK_CAMERA ][0] = VK_F2;
	m_key[KEYRANK_CAMERA ][1] = VK_BUTTON3;
	m_key[KEYRANK_NEAR   ][0] = VK_ADD;
	m_key[KEYRANK_AWAY   ][0] = VK_SUBTRACT;
	m_key[KEYRANK_QUIT   ][0] = VK_ESCAPE;
	m_key[KEYRANK_HELP   ][0] = VK_F1;
	m_key[KEYRANK_CBOT   ][0] = VK_F3;
	m_key[KEYRANK_SPEED10][0] = VK_F4;
	m_key[KEYRANK_SPEED15][0] = VK_F5;
	m_key[KEYRANK_SPEED20][0] = VK_F6;
//	m_key[KEYRANK_SPEED30][0] = VK_F7;
}

// Modifie une touche.

void CD3DApplication::SetKey(int keyRank, int option, int key)
{
	if ( keyRank <  0  ||
		 keyRank >= 50 )  return;

	if ( option <  0 ||
		 option >= 2 )  return;

	m_key[keyRank][option] = key;
}

// Donne une touche.

int CD3DApplication::RetKey(int keyRank, int option)
{
	if ( keyRank <  0  ||
		 keyRank >= 50 )  return 0;

	if ( option <  0 ||
		 option >= 2 )  return 0;

	return m_key[keyRank][option];
}


// Gestion de la force de l'effet FFB.

void CD3DApplication::SetForce(float force)
{
	m_FFBforce = force;
}

float CD3DApplication::RetForce()
{
	return m_FFBforce;
}

// Gestion du force feedback. Il faut appeler SetFFB avant SetJoystick.

void CD3DApplication::SetFFB(BOOL bMode)
{
	m_bFFB = bMode;
}

BOOL CD3DApplication::RetFFB()
{
	return m_bFFB;
}

// Utilise le joystick ou le clavier.
// 0=clavier, 1=volant, 2=joypad

void CD3DApplication::SetJoystick(int mode)
{
	m_joystick = mode;

	if ( m_joystick != 0 )  // joystick ?
	{
		if ( !InitDirectInput(m_instance, m_hWnd, m_bFFB) )  // initialise joystick
		{
			m_joystick = 0;
		}
		else
		{
			if ( m_joystick == 1 )  // volant ?
			{
				if ( m_key[KEYRANK_UP][0] >= VK_BUTTON1  &&
					 m_key[KEYRANK_UP][0] <= VK_BUTTON32 )
				{
					m_key[KEYRANK_UP][0] = m_key[KEYRANK_UP][1];
					m_key[KEYRANK_UP][1] = 0;
					if ( m_key[KEYRANK_UP][0] == 0 )
					{
						m_key[KEYRANK_UP][0] = VK_UP;
					}
				}
				if ( m_key[KEYRANK_UP][1] >= VK_BUTTON1  &&
					 m_key[KEYRANK_UP][1] <= VK_BUTTON32 )
				{
					m_key[KEYRANK_UP][1] = 0;
				}
				if ( m_key[KEYRANK_DOWN][0] >= VK_BUTTON1  &&
					 m_key[KEYRANK_DOWN][0] <= VK_BUTTON32 )
				{
					m_key[KEYRANK_DOWN][0] = m_key[KEYRANK_DOWN][1];
					m_key[KEYRANK_DOWN][1] = 0;
					if ( m_key[KEYRANK_DOWN][0] == 0 )
					{
						m_key[KEYRANK_DOWN][0] = VK_DOWN;
					}
				}
				if ( m_key[KEYRANK_DOWN][1] >= VK_BUTTON1  &&
					 m_key[KEYRANK_DOWN][1] <= VK_BUTTON32 )
				{
					m_key[KEYRANK_DOWN][1] = 0;
				}
			}
			if ( m_joystick == 2 )  // joypad ?
			{
				if ( (m_key[KEYRANK_UP][0] < VK_BUTTON1 ||
					  m_key[KEYRANK_UP][0] > VK_BUTTON32) &&
					 (m_key[KEYRANK_UP][1] < VK_BUTTON1 ||
					  m_key[KEYRANK_UP][1] > VK_BUTTON32) )
				{
					m_key[KEYRANK_UP][1] = VK_BUTTON8;
				}
				if ( (m_key[KEYRANK_DOWN][0] < VK_BUTTON1 ||
					  m_key[KEYRANK_DOWN][0] > VK_BUTTON32) &&
					 (m_key[KEYRANK_DOWN][1] < VK_BUTTON1 ||
					  m_key[KEYRANK_DOWN][1] > VK_BUTTON32) )
				{
					m_key[KEYRANK_DOWN][1] = VK_BUTTON7;
				}
			}
			SetAcquire(TRUE);
			SetTimer(m_hWnd, 0, 1000/30, NULL);
		}
	}
	else	// clavier ?
	{
        KillTimer(m_hWnd, 0);
		SetAcquire(FALSE);
		FreeDirectInput();
	}
}

int CD3DApplication::RetJoystick()
{
	return m_joystick;
}

BOOL CD3DApplication::SetJoyForces(float forceX, float forceY)
{
	float	force;

	force = 0.2f+m_FFBforce*0.8f;
	return ::SetJoyForces(forceX*force, forceY*force);
}


// Message handling function.

LRESULT CD3DApplication::MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam )
{
    HRESULT		hr;
	DIJOYSTATE	js;
	int			i;

	// La touche F10 envoie un autre message pour activer
	// le menu dans les applications Windows standard !
	if ( uMsg == WM_SYSKEYDOWN && wParam == VK_F10 )
	{
		uMsg = WM_KEYDOWN;
	}
	if ( uMsg == WM_SYSKEYUP && wParam == VK_F10 )
	{
		uMsg = WM_KEYUP;
	}

	// Mange l'�v�nement "menu" envoy� par Alt ou F10.
	if ( uMsg == WM_SYSCOMMAND && wParam == SC_KEYMENU )
	{
		return 0;
	}

	if ( uMsg == WM_KEYDOWN || uMsg == WM_KEYUP )
	{
		if ( GetKeyState(VK_SHIFT) & 0x8000 )
		{
			m_keyState |= KS_SHIFT;
		}
		else
		{
			m_keyState &= ~KS_SHIFT;
		}

		if ( GetKeyState(VK_CONTROL) & 0x8000 )
		{
			m_keyState |= KS_CONTROL;
		}
		else
		{
			m_keyState &= ~KS_CONTROL;
		}
	}

	switch( uMsg )
	{
		case WM_KEYDOWN:
			if ( wParam == m_key[KEYRANK_UP   ][0] )  m_axeKeyY =  1.0f;
			if ( wParam == m_key[KEYRANK_UP   ][1] )  m_axeKeyY =  1.0f;
			if ( wParam == m_key[KEYRANK_DOWN ][0] )  m_axeKeyY = -1.0f;
			if ( wParam == m_key[KEYRANK_DOWN ][1] )  m_axeKeyY = -1.0f;
			if ( wParam == m_key[KEYRANK_LEFT ][0] )  m_axeKeyX = -1.0f;
			if ( wParam == m_key[KEYRANK_LEFT ][1] )  m_axeKeyX = -1.0f;
			if ( wParam == m_key[KEYRANK_RIGHT][0] )  m_axeKeyX =  1.0f;
			if ( wParam == m_key[KEYRANK_RIGHT][1] )  m_axeKeyX =  1.0f;
			if ( wParam == m_key[KEYRANK_BRAKE][0] )  m_axeKeyW =  1.0f;
			if ( wParam == m_key[KEYRANK_BRAKE][1] )  m_axeKeyW =  1.0f;
			if ( wParam == m_key[KEYRANK_NEAR ][0] )  m_keyState |= KS_NUMPLUS;
			if ( wParam == m_key[KEYRANK_NEAR ][1] )  m_keyState |= KS_NUMPLUS;
			if ( wParam == m_key[KEYRANK_AWAY ][0] )  m_keyState |= KS_NUMMINUS;
			if ( wParam == m_key[KEYRANK_AWAY ][1] )  m_keyState |= KS_NUMMINUS;
			if ( wParam == VK_PRIOR                )  m_keyState |= KS_PAGEUP;
			if ( wParam == VK_NEXT                 )  m_keyState |= KS_PAGEDOWN;
//?			if ( wParam == VK_SHIFT                )  m_keyState |= KS_SHIFT;
//?			if ( wParam == VK_CONTROL              )  m_keyState |= KS_CONTROL;
			if ( wParam == VK_NUMPAD8              )  m_keyState |= KS_NUMUP;
			if ( wParam == VK_NUMPAD2              )  m_keyState |= KS_NUMDOWN;
			if ( wParam == VK_NUMPAD4              )  m_keyState |= KS_NUMLEFT;
			if ( wParam == VK_NUMPAD6              )  m_keyState |= KS_NUMRIGHT;
			break;

		case WM_KEYUP:
			if ( wParam == m_key[KEYRANK_UP   ][0] )  m_axeKeyY = 0.0f;
			if ( wParam == m_key[KEYRANK_UP   ][1] )  m_axeKeyY = 0.0f;
			if ( wParam == m_key[KEYRANK_DOWN ][0] )  m_axeKeyY = 0.0f;
			if ( wParam == m_key[KEYRANK_DOWN ][1] )  m_axeKeyY = 0.0f;
			if ( wParam == m_key[KEYRANK_LEFT ][0] )  m_axeKeyX = 0.0f;
			if ( wParam == m_key[KEYRANK_LEFT ][1] )  m_axeKeyX = 0.0f;
			if ( wParam == m_key[KEYRANK_RIGHT][0] )  m_axeKeyX = 0.0f;
			if ( wParam == m_key[KEYRANK_RIGHT][1] )  m_axeKeyX = 0.0f;
			if ( wParam == m_key[KEYRANK_BRAKE][0] )  m_axeKeyW = 0.0f;
			if ( wParam == m_key[KEYRANK_BRAKE][1] )  m_axeKeyW = 0.0f;
			if ( wParam == m_key[KEYRANK_NEAR ][0] )  m_keyState &= ~KS_NUMPLUS;
			if ( wParam == m_key[KEYRANK_NEAR ][1] )  m_keyState &= ~KS_NUMPLUS;
			if ( wParam == m_key[KEYRANK_AWAY ][0] )  m_keyState &= ~KS_NUMMINUS;
			if ( wParam == m_key[KEYRANK_AWAY ][1] )  m_keyState &= ~KS_NUMMINUS;
			if ( wParam == VK_PRIOR                )  m_keyState &= ~KS_PAGEUP;
			if ( wParam == VK_NEXT                 )  m_keyState &= ~KS_PAGEDOWN;
//?			if ( wParam == VK_SHIFT                )  m_keyState &= ~KS_SHIFT;
//?			if ( wParam == VK_CONTROL              )  m_keyState &= ~KS_CONTROL;
			if ( wParam == VK_NUMPAD8              )  m_keyState &= ~KS_NUMUP;
			if ( wParam == VK_NUMPAD2              )  m_keyState &= ~KS_NUMDOWN;
			if ( wParam == VK_NUMPAD4              )  m_keyState &= ~KS_NUMLEFT;
			if ( wParam == VK_NUMPAD6              )  m_keyState &= ~KS_NUMRIGHT;
			break;

		case WM_LBUTTONDOWN:
			m_keyState |= KS_MLEFT;
			break;

		case WM_RBUTTONDOWN:
			m_keyState |= KS_MRIGHT;
			break;

		case WM_LBUTTONUP:
			m_keyState &= ~KS_MLEFT;
			break;

		case WM_RBUTTONUP:
			m_keyState &= ~KS_MRIGHT;
			break;

        case WM_PAINT:
            // Handle paint messages when the app is not ready
            if( m_pFramework && !m_bReady )
            {
                if( m_pDeviceInfo->bWindowed )
                    m_pFramework->ShowFrame();
                else
                    m_pFramework->FlipToGDISurface( TRUE );
            }
            break;

        case WM_MOVE:
            // If in windowed mode, move the Framework's window
            if( m_pFramework && m_bActive && m_bReady && m_pDeviceInfo->bWindowed )
                m_pFramework->Move( (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) );
            break;

        case WM_SIZE:
            // Check to see if we are losing our window...
            if( SIZE_MAXHIDE==wParam || SIZE_MINIMIZED==wParam )
			{
                m_bActive = FALSE;
			}
            else
			{
                m_bActive = TRUE;
			}
//?			char s[100];
//?			sprintf(s, "WM_SIZE %d %d %d\n", m_bActive, m_bReady, m_pDeviceInfo->bWindowed);
//?			OutputDebugString(s);

            // A new window size will require a new backbuffer
            // size, so the 3D structures must be changed accordingly.
            if( m_bActive && m_bReady && m_pDeviceInfo->bWindowed )
            {
                m_bReady = FALSE;

//?				OutputDebugString("WM_SIZE Change3DEnvironment\n");
                if( FAILED( hr = Change3DEnvironment() ) )
                    return 0;

                m_bReady = TRUE;
            }
            break;

        case WM_TIMER:
			if ( m_bActivateApp && m_joystick != 0 )
			{
                if ( UpdateInputState(js) )
				{
					m_axeJoy.x =  js.lX/1000.0f+js.lRz/1000.0f;  // tourner
					m_axeJoy.y = -js.lY/1000.0f;  // avancer
					m_axeJoy.z = -js.rglSlider[0]/1000.0f;  // monter

					if ( m_axeJoy.x > 0.5f && !m_bJoyRight )
					{
						m_bJoyRight = TRUE;
						PostMessage(m_hWnd, WM_KEYDOWN, VK_JRIGHT, 0);
					}
					if ( m_axeJoy.x < 0.3f && m_bJoyRight )
					{
						m_bJoyRight = FALSE;
//?						PostMessage(m_hWnd, WM_KEYUP, VK_JRIGHT, 0);
					}

					if ( m_axeJoy.x < -0.5f && !m_bJoyLeft )
					{
						m_bJoyLeft = TRUE;
						PostMessage(m_hWnd, WM_KEYDOWN, VK_JLEFT, 0);
					}
					if ( m_axeJoy.x > -0.3f && m_bJoyLeft )
					{
						m_bJoyLeft = FALSE;
//?						PostMessage(m_hWnd, WM_KEYUP, VK_JLEFT, 0);
					}

					if ( m_axeJoy.y > 0.5f && !m_bJoyUp )
					{
						m_bJoyUp = TRUE;
						PostMessage(m_hWnd, WM_KEYDOWN, VK_JUP, 0);
					}
					if ( m_axeJoy.y < 0.3f && m_bJoyUp )
					{
						m_bJoyUp = FALSE;
//?						PostMessage(m_hWnd, WM_KEYUP, VK_JUP, 0);
					}

					if ( m_axeJoy.y < -0.5f && !m_bJoyDown )
					{
						m_bJoyDown = TRUE;
						PostMessage(m_hWnd, WM_KEYDOWN, VK_JDOWN, 0);
					}
					if ( m_axeJoy.y > -0.3f && m_bJoyDown )
					{
						m_bJoyDown = FALSE;
//?						PostMessage(m_hWnd, WM_KEYUP, VK_JDOWN, 0);
					}

//?					m_axeJoy.x = Neutral(m_axeJoy.x, 0.2f);
					m_axeJoy.y = Neutral(m_axeJoy.y, 0.2f);
					m_axeJoy.z = Neutral(m_axeJoy.z, 0.2f);

					// Si les gaz sont sur un bouton du joypad,
					// ignore l'axe Y du joystick !
					if ( (m_key[KEYRANK_UP][0] >= VK_BUTTON1  &&
						  m_key[KEYRANK_UP][0] <= VK_BUTTON32 ) ||
						 (m_key[KEYRANK_UP][1] >= VK_BUTTON1  &&
						  m_key[KEYRANK_UP][1] <= VK_BUTTON32 ) )
					{
						m_axeJoy.y = 0.0f;
					}

//?					char s[100];
//?					sprintf(s, "x=%d y=%d z=%  x=%d y=%d z=%d\n", js.lX,js.lY,js.lZ,js.lRx,js.lRy,js.lRz);
//?					OutputDebugString(s);

					for ( i=0 ; i<32 ; i++ )
					{
						if ( js.rgbButtons[i] != 0 && !m_bJoyButton[i] )
						{
							m_bJoyButton[i] = TRUE;
							PostMessage(m_hWnd, WM_KEYDOWN, VK_BUTTON1+i, 0);
						}
						if ( js.rgbButtons[i] == 0 && m_bJoyButton[i] )
						{
							m_bJoyButton[i] = FALSE;
							PostMessage(m_hWnd, WM_KEYUP, VK_BUTTON1+i, 0);
						}
					}
				}
				else
				{
					OutputDebugString("UpdateInputState error\n");
				}
			}
			break;

        case WM_ACTIVATE:
            if( LOWORD(wParam) == WA_INACTIVE )
			{
				m_bActivateApp = FALSE;
			}
            else
			{
				m_bActivateApp = TRUE;
			}

			if ( m_bActivateApp && m_joystick != 0 )
			{
				SetAcquire(TRUE);  // r�-active le joystick
			}
			break;

		case MM_MCINOTIFY:
			if ( wParam == MCI_NOTIFY_SUCCESSFUL )
			{
				OutputDebugString("Event MM_MCINOTIFY\n");
				m_pSound->SuspendMusic();
				m_pSound->RestartMusic();
			}
			break;

        case WM_SETCURSOR:
            // Prevent a cursor in fullscreen mode
            if( m_bActive && m_bReady && !m_pDeviceInfo->bWindowed )
            {
//?             SetCursor(NULL);
                return 1;
            }
            break;

        case WM_ENTERMENULOOP:
            // Pause the app when menus are displayed
            Pause(TRUE);
            break;
        case WM_EXITMENULOOP:
            Pause(FALSE);
            break;

        case WM_ENTERSIZEMOVE:
            // Halt frame movement while the app is sizing or moving
			m_pD3DEngine->TimeEnterGel();
            break;
        case WM_EXITSIZEMOVE:
			m_pD3DEngine->TimeExitGel();
            break;

        case WM_NCHITTEST:
            // Prevent the user from selecting the menu in fullscreen mode
            if( !m_pDeviceInfo->bWindowed )
                return HTCLIENT;

            break;

        case WM_POWERBROADCAST:
            switch( wParam )
            {
                case PBT_APMQUERYSUSPEND:
                    // At this point, the app should save any data for open
                    // network connections, files, etc.., and prepare to go into
                    // a suspended mode.
                    return OnQuerySuspend( (DWORD)lParam );

                case PBT_APMRESUMESUSPEND:
                    // At this point, the app should recover any data, network
                    // connections, files, etc.., and resume running from when
                    // the app was suspended.
                    return OnResumeSuspend( (DWORD)lParam );
            }
            break;

        case WM_SYSCOMMAND:
            // Prevent moving/sizing and power loss in fullscreen mode
            switch( wParam )
            {
                case SC_MOVE:
                case SC_SIZE:
                case SC_MAXIMIZE:
                case SC_MONITORPOWER:
                    if( FALSE == m_pDeviceInfo->bWindowed )
                        return 1;
                    break;
            }
            break;

        case WM_COMMAND:
            switch( LOWORD(wParam) )
            {
                case IDM_CHANGEDEVICE:
                    // Display the device-selection dialog box.
                    if( m_bActive && m_bReady )
                    {
                        Pause(TRUE);

                        if( SUCCEEDED( D3DEnum_UserChangeDevice( &m_pDeviceInfo ) ) )
                        {
                            if( FAILED( hr = Change3DEnvironment() ) )
                                return 0;
                        }
                        Pause(FALSE);
                    }
                    return 0;

                case IDM_ABOUT:
                    // Display the About box
                    Pause(TRUE);
                    DialogBox( (HINSTANCE)GetWindowLong( hWnd, GWL_HINSTANCE ),
                               MAKEINTRESOURCE(IDD_ABOUT), hWnd, AboutProc );
                    Pause(FALSE);
                    return 0;

                case IDM_EXIT:
                    // Recieved key/menu command to exit app
                    SendMessage( hWnd, WM_CLOSE, 0, 0 );
                    return 0;
            }
            break;

        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 100;
            ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 100;
            break;

        case WM_CLOSE:
            DestroyWindow( hWnd );
            return 0;

        case WM_DESTROY:
            Cleanup3DEnvironment();
            PostQuitMessage(0);
            return 0;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

            
// Enumeration function to report valid pixel formats for z-buffers.

HRESULT WINAPI EnumZBufferFormatsCallback(DDPIXELFORMAT* pddpf,
										  VOID* pContext)
{
    DDPIXELFORMAT* pddpfOut = (DDPIXELFORMAT*)pContext;

	char s[100];
	sprintf(s, "EnumZBufferFormatsCallback %d\n", pddpf->dwRGBBitCount);
	OutputDebugString(s);

    if( pddpfOut->dwRGBBitCount == pddpf->dwRGBBitCount )
    {
        (*pddpfOut) = (*pddpf);
        return D3DENUMRET_CANCEL;
    }

    return D3DENUMRET_OK;
}

// Internal function called by Create() to make and attach a zbuffer
// to the renderer.

HRESULT CD3DApplication::CreateZBuffer(GUID* pDeviceGUID)
{
    HRESULT hr;

    // Check if the device supports z-bufferless hidden surface removal. If so,
    // we don't really need a z-buffer
    D3DDEVICEDESC7 ddDesc;
    m_pD3DDevice->GetCaps( &ddDesc );
    if( ddDesc.dpcTriCaps.dwRasterCaps & D3DPRASTERCAPS_ZBUFFERLESSHSR )
        return S_OK;

    // Get z-buffer dimensions from the render target
    DDSURFACEDESC2 ddsd;
    ddsd.dwSize = sizeof(ddsd);
    m_pddsRenderTarget->GetSurfaceDesc( &ddsd );

    // Setup the surface desc for the z-buffer.
    ddsd.dwFlags        = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    ddsd.ddpfPixelFormat.dwSize = 0;  // Tag the pixel format as unitialized

    // Get an appropiate pixel format from enumeration of the formats. On the
    // first pass, we look for a zbuffer dpeth which is equal to the frame
    // buffer depth (as some cards unfornately require this).
    m_pD3D->EnumZBufferFormats( *pDeviceGUID, EnumZBufferFormatsCallback,
                                (VOID*)&ddsd.ddpfPixelFormat );
    if( 0 == ddsd.ddpfPixelFormat.dwSize )
    {
        // Try again, just accepting any 16-bit zbuffer
        ddsd.ddpfPixelFormat.dwRGBBitCount = 16;
        m_pD3D->EnumZBufferFormats( *pDeviceGUID, EnumZBufferFormatsCallback,
                                    (VOID*)&ddsd.ddpfPixelFormat );
            
        if( 0 == ddsd.ddpfPixelFormat.dwSize )
        {
            DEBUG_MSG( _T("Device doesn't support requested zbuffer format") );
            return D3DFWERR_NOZBUFFER;
        }
    }

    // Create and attach a z-buffer
    if( FAILED( hr = m_pDD->CreateSurface( &ddsd, &m_pddsDepthBuffer, NULL ) ) )
    {
        DEBUG_MSG( _T("Error: Couldn't create a ZBuffer surface") );
        if( hr != DDERR_OUTOFVIDEOMEMORY )
            return D3DFWERR_NOZBUFFER;
        DEBUG_MSG( _T("Error: Out of video memory") );
        return DDERR_OUTOFVIDEOMEMORY;
    }

    if( FAILED( m_pddsRenderTarget->AddAttachedSurface( m_pddsDepthBuffer ) ) )
    {
        DEBUG_MSG( _T("Error: Couldn't attach zbuffer to render surface") );
        return D3DFWERR_NOZBUFFER;
    }

    // Finally, this call rebuilds internal structures
    if( FAILED( m_pD3DDevice->SetRenderTarget( m_pddsRenderTarget, 0L ) ) )
    {
        DEBUG_MSG( _T("Error: SetRenderTarget() failed after attaching zbuffer!") );
        return D3DFWERR_NOZBUFFER;
    }

    return S_OK;
}

// Initializes the sample framework, then calls the app-specific function
// to initialize device specific objects. This code is structured to
// handled any errors that may occur duing initialization.

HRESULT CD3DApplication::Initialize3DEnvironment()
{
    HRESULT		hr;
	DDSCAPS2	ddsCaps2; 
    DWORD		dwFrameworkFlags = 0L;
	DWORD		dwTotal; 
	DWORD		dwFree;

    dwFrameworkFlags |= ( !m_pDeviceInfo->bWindowed ? D3DFW_FULLSCREEN : 0L );
    dwFrameworkFlags |= (  m_pDeviceInfo->bStereo   ? D3DFW_STEREO     : 0L );
    dwFrameworkFlags |= (  m_bAppUseZBuffer         ? D3DFW_ZBUFFER    : 0L );

    // Initialize the D3D framework
    if( SUCCEEDED( hr = m_pFramework->Initialize( m_hWnd,
                     m_pDeviceInfo->pDriverGUID, m_pDeviceInfo->pDeviceGUID,
                     &m_pDeviceInfo->ddsdFullscreenMode, dwFrameworkFlags ) ) )
    {
        m_pDD        = m_pFramework->GetDirectDraw();
        m_pD3D       = m_pFramework->GetDirect3D();
        m_pD3DDevice = m_pFramework->GetD3DDevice();

//?		m_pDD->SetCooperativeLevel(m_hWnd, DDSCL_FULLSCREEN);

		m_pD3DEngine->SetD3DDevice(m_pD3DDevice);

		m_pddsRenderTarget = m_pFramework->GetRenderSurface();

		m_ddsdRenderTarget.dwSize = sizeof(m_ddsdRenderTarget);
		m_pddsRenderTarget->GetSurfaceDesc( &m_ddsdRenderTarget );

		// Demande la quantit� de m�moire vid�o.
		ZeroMemory(&ddsCaps2, sizeof(ddsCaps2));
		ddsCaps2.dwCaps = DDSCAPS_TEXTURE; 
		dwTotal = 0;
		hr = m_pDD->GetAvailableVidMem(&ddsCaps2, &dwTotal, &dwFree); 
		m_vidMemTotal = dwTotal;

		// Let the app run its startup code which creates the 3d scene.
		if( SUCCEEDED( hr = m_pD3DEngine->InitDeviceObjects() ) )
		{
//? 		CreateZBuffer(m_pDeviceInfo->pDeviceGUID);
			return S_OK;
		}
		else
		{
			DeleteDeviceObjects();
			m_pFramework->DestroyObjects();
		}
	}

	// If we get here, the first initialization passed failed. If that was with a
	// hardware device, try again using a software rasterizer instead.
	if( m_pDeviceInfo->bHardware )
	{
		// Try again with a software rasterizer
		DisplayFrameworkError( hr, MSGWARN_SWITCHEDTOSOFTWARE );
		D3DEnum_SelectDefaultDevice( &m_pDeviceInfo, D3DENUM_SOFTWAREONLY );
		return Initialize3DEnvironment();
	}
 
	return hr;
}


// Handles driver, device, and/or mode changes for the app.

HRESULT CD3DApplication::Change3DEnvironment()
{
#if 0
	HRESULT hr;
	static BOOL  bOldWindowedState = TRUE;
	static DWORD dwSavedStyle;
	static RECT  rcSaved;

	// Release all scene objects that will be re-created for the new device
	DeleteDeviceObjects();

	// Release framework objects, so a new device can be created
	if( FAILED( hr = m_pFramework->DestroyObjects() ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		SendMessage( m_hWnd, WM_CLOSE, 0, 0 );
		return hr;
	}

	// Check if going from fullscreen to windowed mode, or vice versa.
	if( bOldWindowedState != m_pDeviceInfo->bWindowed )
	{
		if( m_pDeviceInfo->bWindowed )
		{
			// Coming from fullscreen mode, so restore window properties
			SetWindowLong( m_hWnd, GWL_STYLE, dwSavedStyle );
			SetWindowPos( m_hWnd, HWND_NOTOPMOST, rcSaved.left, rcSaved.top,
						  ( rcSaved.right - rcSaved.left ), 
						  ( rcSaved.bottom - rcSaved.top ), SWP_SHOWWINDOW );
		}
		else
		{
			// Going to fullscreen mode, save/set window properties as needed
			dwSavedStyle = GetWindowLong( m_hWnd, GWL_STYLE );
			GetWindowRect( m_hWnd, &rcSaved );
			SetWindowLong( m_hWnd, GWL_STYLE, WS_POPUP|WS_SYSMENU|WS_VISIBLE );
		}

		bOldWindowedState = m_pDeviceInfo->bWindowed;
	}

	// Inform the framework class of the driver change. It will internally
	// re-create valid surfaces, a d3ddevice, etc.
	if( FAILED( hr = Initialize3DEnvironment() ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		SendMessage( m_hWnd, WM_CLOSE, 0, 0 );
		return hr;
	}

	return S_OK;
#else
	HRESULT hr;

	// Release all scene objects that will be re-created for the new device
	DeleteDeviceObjects();

	// Release framework objects, so a new device can be created
	if( FAILED( hr = m_pFramework->DestroyObjects() ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		SendMessage( m_hWnd, WM_CLOSE, 0, 0 );
		return hr;
	}

	if( m_pDeviceInfo->bWindowed )
	{
		SetWindowPos(m_hWnd, HWND_NOTOPMOST, 10, 10, WINDOW_DX, WINDOW_DY, SWP_SHOWWINDOW);
	}

	// Inform the framework class of the driver change. It will internally
	// re-create valid surfaces, a d3ddevice, etc.
	if( FAILED( hr = Initialize3DEnvironment() ) )
	{
		DisplayFrameworkError( hr, MSGERR_APPMUSTEXIT );
		SendMessage( m_hWnd, WM_CLOSE, 0, 0 );
		return hr;
	}

	m_pD3DEngine->ChangeLOD();

	if( m_pDeviceInfo->bWindowed )
	{
		SetNiceMouse(FALSE);  // cache la vilaine souris windows
	}

	return S_OK;
#endif
}



// Fait �voluer tout le jeu.

void CD3DApplication::StepSimul(float rTime)
{
	Event	event;

	if ( rTime == 0.0f )  return;  // (*)
	if ( m_pRobotMain == 0 )  return;

	ZeroMemory(&event, sizeof(Event));
	event.event = EVENT_FRAME;  // dr�le de bug en release "Maximize speed" !!!
	event.rTime = rTime;
	event.axeX = AxeLimit(m_axeKeyX + m_axeJoy.x);
	event.axeY = AxeLimit(m_axeKeyY + m_axeJoy.y);
	event.axeZ = AxeLimit(m_axeKeyZ + m_axeJoy.z);
	event.axeW = m_axeKeyW;
	event.keyState = m_keyState;

//?char s[100];
//?sprintf(s, "StepSimul %.3f\n", event.rTime);
//?OutputDebugString(s);
	m_pRobotMain->EventProcess(event);
}

// (*)	Avec une machine rapide (Athlon 1800+) et Windows XP,
//		un delta t nul se produit parfois, au d�but d'une mission.
//		Ceci a des cons�quences facheuses: suspensions inefficaces
//		lors du choix d'une voiture, cam�ra restant sur le starter, etc.


// Draws the scene.

HRESULT CD3DApplication::Render3DEnvironment()
{
    HRESULT hr;
	float	rTime;

    // Check the cooperative level before rendering
    if( FAILED( hr = m_pDD->TestCooperativeLevel() ) )
    {
        switch( hr )
        {
            case DDERR_EXCLUSIVEMODEALREADYSET:
            case DDERR_NOEXCLUSIVEMODE:
				OutputDebugString("DDERR_EXCLUSIVEMODEALREADYSET\n");
                // Do nothing because some other app has exclusive mode
                return S_OK;

            case DDERR_WRONGMODE:
				OutputDebugString("DDERR_WRONGMODE\n");
                // The display mode changed on us. Resize accordingly
                if( m_pDeviceInfo->bWindowed )
                    return Change3DEnvironment();
                break;
        }
        return hr;
    }

	// Get the relative time, in seconds
	rTime = m_pD3DEngine->TimeGet();
	if ( rTime > MAX_STEP )  rTime = MAX_STEP;  // jamais plus de 0.5s !
	m_aTime += rTime;

#if !USE_THREAD
    if( FAILED( hr = m_pD3DEngine->FrameMove(rTime) ) )
        return hr;

    // FrameMove (animate) the scene
	StepSimul(rTime);
#endif

	// Render the scene.
	if( FAILED( hr = m_pD3DEngine->Render() ) )
		return hr;

	DrawSuppl();

    // Show the frame rate, etc.
    if( m_bShowStats )
        ShowStats();

    // Show the frame on the primary surface.
    if( FAILED( hr = m_pFramework->ShowFrame() ) )
    {
        if( DDERR_SURFACELOST != hr )
            return hr;

        m_pFramework->RestoreSurfaces();
        m_pD3DEngine->RestoreSurfaces();
    }

    return S_OK;
}


// Cleanup scene objects

VOID CD3DApplication::Cleanup3DEnvironment()
{
    m_bActive = FALSE;
    m_bReady  = FALSE;

    if( m_pFramework )
    {
        DeleteDeviceObjects();
        SAFE_DELETE( m_pFramework );

        m_pD3DEngine->FinalCleanup();
    }

    D3DEnum_FreeResources();
//?	FreeDirectInput();
}

// Called when the app is exitting, or the device is being changed,
// this function deletes any device dependant objects.

VOID CD3DApplication::DeleteDeviceObjects()
{
    if( m_pFramework )
    {
        m_pD3DEngine->DeleteDeviceObjects();
	    SAFE_RELEASE( m_pddsDepthBuffer );
    }
}



// Called in to toggle the pause state of the app. This function
// brings the GDI surface to the front of the display, so drawing
// output like message boxes and menus may be displayed.

VOID CD3DApplication::Pause( BOOL bPause )
{
    static DWORD dwAppPausedCount = 0L;

    dwAppPausedCount += ( bPause ? +1 : -1 );
    m_bReady          = ( dwAppPausedCount ? FALSE : TRUE );

    // Handle the first pause request (of many, nestable pause requests)
    if( bPause && ( 1 == dwAppPausedCount ) )
    {
        // Get a surface for the GDI
        if( m_pFramework )
            m_pFramework->FlipToGDISurface( TRUE );

        // Stop the scene from animating
		m_pD3DEngine->TimeEnterGel();
    }

    if( 0 == dwAppPausedCount )
    {
        // Restart the scene
		m_pD3DEngine->TimeExitGel();
    }
}


// Called when the app receives a PBT_APMQUERYSUSPEND message, meaning
// the computer is about to be suspended. At this point, the app should
// save any data for open network connections, files, etc.., and prepare
// to go into a suspended mode.

LRESULT CD3DApplication::OnQuerySuspend( DWORD dwFlags )
{
	OutputDebugString("OnQuerySuspend\n");
    Pause(TRUE);
    return TRUE;
}


// Called when the app receives a PBT_APMRESUMESUSPEND message, meaning
// the computer has just resumed from a suspended state. At this point, 
// the app should recover any data, network connections, files, etc..,
// and resume running from when the app was suspended.

LRESULT CD3DApplication::OnResumeSuspend( DWORD dwData )
{
	OutputDebugString("OnResumeSuspend\n");
    Pause(FALSE);
    return TRUE;
}


// Dessine tous les �l�ments graphiques suppl�mentaires.

void CD3DApplication::DrawSuppl()
{
	HDC			hDC;
	FPOINT		p1, p2;
	POINT		list[3];
	RECT		rect;
	HPEN		hPen;
	HGDIOBJ		old;
	FPOINT		pos;
	float		d;
	int			nbOut;

	if ( FAILED(m_pddsRenderTarget->GetDC(&hDC)) )  return;

	// Affiche le rectangle de s�lection.
	if ( m_pD3DEngine->GetHilite(p1, p2) )
	{
		nbOut = 0;
		if ( p1.x < 0.0f || p1.x > 1.0f )  nbOut ++;
		if ( p1.y < 0.0f || p1.y > 1.0f )  nbOut ++;
		if ( p2.x < 0.0f || p2.x > 1.0f )  nbOut ++;
		if ( p2.y < 0.0f || p2.y > 1.0f )  nbOut ++;
		if ( nbOut <= 2 )
		{
#if 0
			time = Mod(m_aTime, 0.5f);
			if ( time < 0.25f )  d = time*4.0f;
			else                 d = (2.0f-time*4.0f);
#endif
#if 0
			time = Mod(m_aTime, 0.5f);
			if ( time < 0.4f )  d = time/0.4f;
			else                d = 1.0f-(time-0.4f)/0.1f;
#endif
#if 1
			d = 0.5f+sinf(m_aTime*6.0f)*0.5f;
#endif
			d *= (p2.x-p1.x)*0.1f;
			p1.x += d;
			p1.y += d;
			p2.x -= d;
			p2.y -= d;

			hPen = CreatePen(PS_SOLID, 1, RGB(255,255,0));  // jaune
			old = SelectObject(hDC, hPen);

			rect.left   = (int)(p1.x*m_ddsdRenderTarget.dwWidth);
			rect.right  = (int)(p2.x*m_ddsdRenderTarget.dwWidth);
			rect.top    = (int)((1.0f-p2.y)*m_ddsdRenderTarget.dwHeight);
			rect.bottom = (int)((1.0f-p1.y)*m_ddsdRenderTarget.dwHeight);

			list[0].x = rect.left;
			list[0].y = rect.top+(rect.bottom-rect.top)/5;
			list[1].x = rect.left;
			list[1].y = rect.top;
			list[2].x = rect.left+(rect.right-rect.left)/5;
			list[2].y = rect.top;
			Polyline(hDC, list, 3);

			list[0].x = rect.right;
			list[0].y = rect.top+(rect.bottom-rect.top)/5;
			list[1].x = rect.right;
			list[1].y = rect.top;
			list[2].x = rect.right+(rect.left-rect.right)/5;
			list[2].y = rect.top;
			Polyline(hDC, list, 3);

			list[0].x = rect.left;
			list[0].y = rect.bottom+(rect.top-rect.bottom)/5;
			list[1].x = rect.left;
			list[1].y = rect.bottom;
			list[2].x = rect.left+(rect.right-rect.left)/5;
			list[2].y = rect.bottom;
			Polyline(hDC, list, 3);

			list[0].x = rect.right;
			list[0].y = rect.bottom+(rect.top-rect.bottom)/5;
			list[1].x = rect.right;
			list[1].y = rect.bottom;
			list[2].x = rect.right+(rect.left-rect.right)/5;
			list[2].y = rect.bottom;
			Polyline(hDC, list, 3);

			if ( old != 0 )  SelectObject(hDC, old);
			DeleteObject(hPen);
		}
	}

	m_pddsRenderTarget->ReleaseDC(hDC);
}

// Shows frame rate and dimensions of the rendering device.

VOID CD3DApplication::ShowStats()
{
    static FLOAT fFPS      = 0.0f;
    static FLOAT fLastTime = 0.0f;
    static DWORD dwFrames  = 0L;

    // Keep track of the time lapse and frame count
    FLOAT fTime = timeGetTime() * 0.001f; // Get current time in seconds
    ++dwFrames;

    // Update the frame rate once per second
    if( fTime - fLastTime > 1.0f )
    {
        fFPS      = dwFrames / (fTime - fLastTime);
        fLastTime = fTime;
        dwFrames  = 0L;
    }

	int t = m_pD3DEngine->RetStatisticTriangle();

    // Setup the text buffer to write out dimensions
    TCHAR buffer[100];
    sprintf( buffer, _T("%7.02f fps T=%d (%dx%dx%d)"), fFPS, t,
             m_ddsdRenderTarget.dwWidth, m_ddsdRenderTarget.dwHeight, 
             m_ddsdRenderTarget.ddpfPixelFormat.dwRGBBitCount );
    OutputText( 400, 2, buffer );

	int	x, y, i;
	if ( m_pD3DEngine->GetSpriteCoord(x, y) )
	{
	    OutputText( x, y, "+" );
	}

	for ( i=0 ; i<10 ; i++ )
	{
		char* info = m_pD3DEngine->RetInfoText(i);
		x = 50;
		y = m_ddsdRenderTarget.dwHeight-20-i*20;
		OutputText( x, y, info );
	}
}


// Draws text on the window.

VOID CD3DApplication::OutputText( DWORD x, DWORD y, TCHAR* str )
{
    HDC hDC;

    // Get a DC for the surface. Then, write out the buffer
    if( m_pddsRenderTarget )
    {
        if( SUCCEEDED( m_pddsRenderTarget->GetDC(&hDC) ) )
        {
            SetTextColor( hDC, RGB(255,255,0) );
            SetBkMode( hDC, TRANSPARENT );
            ExtTextOut( hDC, x, y, 0, NULL, str, lstrlen(str), NULL );
            m_pddsRenderTarget->ReleaseDC(hDC);
        }
    }
}




// Defines a function that allocates memory for and initializes
// members within a BITMAPINFOHEADER structure

PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp)
{ 
	BITMAP		bmp;
	PBITMAPINFO	pbmi;
	WORD		cClrBits;
 
	// Retrieve the bitmap's color format, width, and height.
	if ( !GetObject(hBmp, sizeof(BITMAP), (LPSTR)&bmp) )
		return 0;
  
	// Convert the color format to a count of bits.
	cClrBits = (WORD)(bmp.bmPlanes * bmp.bmBitsPixel);
 
	     if ( cClrBits ==  1 )  cClrBits =  1;
	else if ( cClrBits <=  4 )  cClrBits =  4;
	else if ( cClrBits <=  8 )  cClrBits =  8;
	else if ( cClrBits <= 16 )  cClrBits = 16;
	else if ( cClrBits <= 24 )  cClrBits = 24;
	else                        cClrBits = 32;
 
	// Allocate memory for the BITMAPINFO structure. (This structure 
	// contains a BITMAPINFOHEADER structure and an array of RGBQUAD data 
	// structures.) 
	if ( cClrBits != 24 )
	{
		 pbmi = (PBITMAPINFO)LocalAlloc(LPTR,
					sizeof(BITMAPINFOHEADER) +
					sizeof(RGBQUAD) * (2^cClrBits));
	}
	// There is no RGBQUAD array for the 24-bit-per-pixel format.
	else
	{
		 pbmi = (PBITMAPINFO)LocalAlloc(LPTR,
					sizeof(BITMAPINFOHEADER));
	}
 
	// Initialize the fields in the BITMAPINFO structure.
	pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbmi->bmiHeader.biWidth = bmp.bmWidth;
	pbmi->bmiHeader.biHeight = bmp.bmHeight;
	pbmi->bmiHeader.biPlanes = bmp.bmPlanes;
	pbmi->bmiHeader.biBitCount = bmp.bmBitsPixel;
	if ( cClrBits < 24 )
		pbmi->bmiHeader.biClrUsed = 2^cClrBits;
  
	// If the bitmap is not compressed, set the BI_RGB flag.
 	pbmi->bmiHeader.biCompression = BI_RGB;
 
	// Compute the number of bytes in the array of color
	// indices and store the result in biSizeImage.
	pbmi->bmiHeader.biSizeImage = (pbmi->bmiHeader.biWidth + 7) /8
								  * pbmi->bmiHeader.biHeight
								  * cClrBits;
 
	// Set biClrImportant to 0, indicating that all of the
	// device colors are important.
	pbmi->bmiHeader.biClrImportant = 0;

	return pbmi;
} 
 
// Defines a function that initializes the remaining structures,
// retrieves the array of palette indices, opens the file, copies
// the data, and closes the file. 

BOOL CreateBMPFile(LPTSTR pszFile, PBITMAPINFO pbi, HBITMAP hBMP, HDC hDC)
{ 
	HANDLE				hf;			// file handle
	BITMAPFILEHEADER	hdr;		// bitmap file-header
	PBITMAPINFOHEADER	pbih;		// bitmap info-header
	LPBYTE				lpBits;		// memory pointer
	DWORD				dwTotal;	// total count of bytes
	DWORD				cb;			// incremental count of bytes
	BYTE*				hp;			// byte pointer
	DWORD				dwTmp; 
 
 
	pbih = (PBITMAPINFOHEADER)pbi;
	lpBits = (LPBYTE)GlobalAlloc(GMEM_FIXED, pbih->biSizeImage);
	if ( !lpBits )  return FALSE;
 
	// Retrieve the color table (RGBQUAD array) and the bits
	// (array of palette indices) from the DIB.
	if ( !GetDIBits(hDC, hBMP, 0, (WORD)pbih->biHeight,
					lpBits, pbi, DIB_RGB_COLORS) )
		return FALSE;
 
	// Create the .BMP file.
	hf = CreateFile(pszFile,
					GENERIC_READ|GENERIC_WRITE,
					(DWORD)0,
					(LPSECURITY_ATTRIBUTES)NULL,
					CREATE_ALWAYS,
					FILE_ATTRIBUTE_NORMAL,
					(HANDLE)NULL);
 
	if ( hf == INVALID_HANDLE_VALUE )
		return FALSE;
 
	hdr.bfType = 0x4d42; // 0x42 = "B" 0x4d = "M"
 
	// Compute the size of the entire file.
	hdr.bfSize = (DWORD)(sizeof(BITMAPFILEHEADER) +
						 pbih->biSize + pbih->biClrUsed
						 * sizeof(RGBQUAD) + pbih->biSizeImage);
 
	hdr.bfReserved1 = 0;
	hdr.bfReserved2 = 0;
 
	// Compute the offset to the array of color indices.
	hdr.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) +
					pbih->biSize + pbih->biClrUsed
					* sizeof (RGBQUAD);
 
	// Copy the BITMAPFILEHEADER into the .BMP file.
	if ( !WriteFile(hf, (LPVOID)&hdr, sizeof(BITMAPFILEHEADER),
					(LPDWORD)&dwTmp, (LPOVERLAPPED)NULL) )
		return FALSE;
 
	// Copy the BITMAPINFOHEADER and RGBQUAD array into the file.
	if ( !WriteFile(hf, (LPVOID)pbih, sizeof(BITMAPINFOHEADER)
					+ pbih->biClrUsed * sizeof (RGBQUAD),
					(LPDWORD) &dwTmp, (LPOVERLAPPED) NULL) )
		return FALSE;
 
	// Copy the array of color indices into the .BMP file.
	dwTotal = cb = pbih->biSizeImage;
	hp = lpBits;
	while ( cb > 10000 )
	{ 
		if ( !WriteFile(hf, (LPSTR)hp, (int)10000,
						(LPDWORD)&dwTmp, (LPOVERLAPPED) NULL) )
			return FALSE;
		cb -= 10000; 
		hp += 10000; 
	} 
	if ( !WriteFile(hf, (LPSTR)hp, (int)cb,
					(LPDWORD)&dwTmp, (LPOVERLAPPED) NULL) )
		 return FALSE;
 
	// Close the .BMP file.
 	if ( !CloseHandle(hf) )
		 return FALSE;
 
	// Free memory.
	GlobalFree((HGLOBAL)lpBits);
	return TRUE;
}

// Ecrit un fichier .BMP copie d'�cran.

BOOL CD3DApplication::WriteScreenShot(char *filename, int width, int height)
{
	D3DVIEWPORT7	vp;
	HDC				hDC;
	HDC				hDCImage;
	HBITMAP			hb;
	PBITMAPINFO		info;
	int				dx, dy;

	m_pD3DDevice->GetViewport(&vp);
	dx = vp.dwWidth;
	dy = vp.dwHeight;

	if ( FAILED(m_pddsRenderTarget->GetDC(&hDC)) )  return FALSE;

	hDCImage = CreateCompatibleDC(hDC);
	if ( hDCImage == 0 )
	{
		m_pddsRenderTarget->ReleaseDC(hDC);
		return FALSE;
	}

	hb = CreateCompatibleBitmap(hDC, width, height);
	if ( hb == 0 )
	{
		DeleteDC(hDCImage);
		m_pddsRenderTarget->ReleaseDC(hDC);
		return FALSE;
	}

	SelectObject(hDCImage, hb);
	StretchBlt(hDCImage, 0, 0, width, height, hDC, 0, 0, dx, dy, SRCCOPY);

	info = CreateBitmapInfoStruct(hb);
	if ( info == 0 )
	{
		DeleteObject(hb);
		DeleteDC(hDCImage);
		m_pddsRenderTarget->ReleaseDC(hDC);
		return FALSE;
	}

	CreateBMPFile(filename, info, hb, hDCImage);

	DeleteObject(hb);
    DeleteDC(hDCImage);
	m_pddsRenderTarget->ReleaseDC(hDC);
	return TRUE;
}



// Effectue la liste de tous les devices graphiques disponibles.
// Pour le device s�lectionn�, donne la liste des modes plein �cran
// possibles.
// buf* --> nom1<0> nom2<0> <0>

BOOL CD3DApplication::EnumDevices(char *bufDevices,  int lenDevices,
								  char *bufModes,    int lenModes,
								  int &totalDevices, int &selectDevices,
								  int &totalModes,   int &selectModes)
{
	D3DEnum_DeviceInfo*	pDeviceList;
	D3DEnum_DeviceInfo*	pDevice;
	DDSURFACEDESC2*		pddsdMode;
	DWORD				numDevices, device, mode;
	int					len;
	char				text[100];

	D3DEnum_GetDevices(&pDeviceList, &numDevices);

	selectDevices = -1;
	selectModes = -1;
	totalModes = 0;
	for( device=0 ; device<numDevices ; device++ )
	{
		pDevice = &pDeviceList[device];

		len = strlen(pDevice->strDesc)+1;
		if ( len >= lenDevices )  break;  // bufDevices plein !
		strcpy(bufDevices, pDevice->strDesc);
		bufDevices += len;
		lenDevices -= len;

		if ( pDevice == m_pDeviceInfo )  // select device ?
		{
			selectDevices = device;

			for( mode=0 ; mode<pDevice->dwNumModes ; mode++ )
			{
				pddsdMode = &pDevice->pddsdModes[mode];

				sprintf(text, "%ld x %ld x %ld",
								pddsdMode->dwWidth,
								pddsdMode->dwHeight,
								pddsdMode->ddpfPixelFormat.dwRGBBitCount);

				len = strlen(text)+1;
				if ( len >= lenModes )  break;  // bufModes plein !
				strcpy(bufModes, text);
				bufModes += len;
				lenModes -= len;

				if ( mode == m_pDeviceInfo->dwCurrentMode )  // select mode ?
				{
					selectModes = mode;
				}
			}
			bufModes[0] = 0;
			totalModes = pDevice->dwNumModes;
		}
	}
	bufDevices[0] = 0;
	totalDevices = numDevices;

	return TRUE;
}

// Indique si on est en mode plein �cran.

BOOL CD3DApplication::RetFullScreen()
{
	return !m_pDeviceInfo->bWindowed;
}

// Change le mode graphique.

BOOL CD3DApplication::ChangeDevice(char *deviceName, char *modeName,
								   BOOL bFull)
{
	D3DEnum_DeviceInfo*	pDeviceList;
	D3DEnum_DeviceInfo*	pDevice;
	DDSURFACEDESC2*		pddsdMode;
	DWORD				numDevices, device, mode;
	HRESULT				hr;
	char				text[100];

	D3DEnum_GetDevices(&pDeviceList, &numDevices);

	for( device=0 ; device<numDevices ; device++ )
	{
		pDevice = &pDeviceList[device];

		if ( strcmp(pDevice->strDesc, deviceName) == 0 )  // device found ?
		{
			for( mode=0 ; mode<pDevice->dwNumModes ; mode++ )
			{
				pddsdMode = &pDevice->pddsdModes[mode];

				sprintf(text, "%ld x %ld x %ld",
								pddsdMode->dwWidth,
								pddsdMode->dwHeight,
								pddsdMode->ddpfPixelFormat.dwRGBBitCount);

				if ( strcmp(text, modeName) == 0 )  // mode found ?
				{
					m_pDeviceInfo               = pDevice;
					pDevice->bWindowed          = !bFull;
					pDevice->dwCurrentMode      = mode;
					pDevice->ddsdFullscreenMode = pDevice->pddsdModes[mode];

					m_bReady = FALSE;

					if ( FAILED( hr = Change3DEnvironment() ) )
					{
						return FALSE;
					}

					SetProfileString("Device", "Name", deviceName);
					SetProfileString("Device", "Mode", modeName);
					SetProfileInt("Device", "FullScreen", bFull);
					m_bReady = TRUE;
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}



// Displays error messages in a message box.

VOID CD3DApplication::DisplayFrameworkError( HRESULT hr, DWORD dwType )
{
    TCHAR strMsg[512];

    switch( hr )
    {
        case D3DENUMERR_ENGINE:
            lstrcpy( strMsg, _T("Could not create 3D Engine application!") );
            break;
        case D3DENUMERR_ROBOT:
            lstrcpy( strMsg, _T("Could not create Robot application!") );
            break;
        case D3DENUMERR_NODIRECTDRAW:
            lstrcpy( strMsg, _T("Could not create DirectDraw!") );
            break;
        case D3DENUMERR_NOCOMPATIBLEDEVICES:
            lstrcpy( strMsg, _T("Could not find any compatible Direct3D\n"
                     "devices.") );
            break;
        case D3DENUMERR_SUGGESTREFRAST:
            lstrcpy( strMsg, _T("Could not find any compatible devices.\n\n"
                     "Try enabling the reference rasterizer using\n"
                     "EnableRefRast.reg.") );
            break;
        case D3DENUMERR_ENUMERATIONFAILED:
            lstrcpy( strMsg, _T("Enumeration failed. Your system may be in an\n"
                     "unstable state and need to be rebooted") );
            break;
        case D3DFWERR_INITIALIZATIONFAILED:
            lstrcpy( strMsg, _T("Generic initialization error.\n\nEnable "
                     "debug output for detailed information.") );
            break;
        case D3DFWERR_NODIRECTDRAW:
            lstrcpy( strMsg, _T("No DirectDraw") );
            break;
        case D3DFWERR_NODIRECT3D:
            lstrcpy( strMsg, _T("No Direct3D") );
            break;
        case D3DFWERR_INVALIDMODE:
            lstrcpy( strMsg, _T("BuzzingCars requires a 16-bit (or higher) "
                                "display mode\nto run in a window.\n\nPlease "
                                "switch your desktop settings accordingly.") );
            break;
        case D3DFWERR_COULDNTSETCOOPLEVEL:
            lstrcpy( strMsg, _T("Could not set Cooperative Level") );
            break;
        case D3DFWERR_NO3DDEVICE:
            lstrcpy( strMsg, _T("Could not create the Direct3DDevice object.") );
            
            if( MSGWARN_SWITCHEDTOSOFTWARE == dwType )
                lstrcat( strMsg, _T("\nThe 3D hardware chipset may not support"
                                    "\nrendering in the current display mode.") );
            break;
        case D3DFWERR_NOZBUFFER:
            lstrcpy( strMsg, _T("No ZBuffer") );
            break;
        case D3DFWERR_INVALIDZBUFFERDEPTH:
            lstrcpy( strMsg, _T("Invalid Z-buffer depth. Try switching modes\n"
                     "from 16- to 32-bit (or vice versa)") );
            break;
        case D3DFWERR_NOVIEWPORT:
            lstrcpy( strMsg, _T("No Viewport") );
            break;
        case D3DFWERR_NOPRIMARY:
            lstrcpy( strMsg, _T("No primary") );
            break;
        case D3DFWERR_NOCLIPPER:
            lstrcpy( strMsg, _T("No Clipper") );
            break;
        case D3DFWERR_BADDISPLAYMODE:
            lstrcpy( strMsg, _T("Bad display mode") );
            break;
        case D3DFWERR_NOBACKBUFFER:
            lstrcpy( strMsg, _T("No backbuffer") );
            break;
        case D3DFWERR_NONZEROREFCOUNT:
            lstrcpy( strMsg, _T("A DDraw object has a non-zero reference\n"
                     "count (meaning it was not properly cleaned up)." ) );
            break;
        case D3DFWERR_NORENDERTARGET:
            lstrcpy( strMsg, _T("No render target") );
            break;
        case E_OUTOFMEMORY:
            lstrcpy( strMsg, _T("Not enough memory!") );
            break;
        case DDERR_OUTOFVIDEOMEMORY:
            lstrcpy( strMsg, _T("There was insufficient video memory "
                     "to use the\nhardware device.") );
            break;
        default:
            lstrcpy( strMsg, _T("Generic application error.\n\nEnable "
                     "debug output for detailed information.") );
    }

    if( MSGERR_APPMUSTEXIT == dwType )
    {
        lstrcat( strMsg, _T("\n\nBuzzingCars will now exit.") );
        MessageBox( NULL, strMsg, m_strWindowTitle, MB_ICONERROR|MB_OK );
    }
    else
    {
        if( MSGWARN_SWITCHEDTOSOFTWARE == dwType )
            lstrcat( strMsg, _T("\n\nSwitching to software rasterizer.") );
        MessageBox( NULL, strMsg, m_strWindowTitle, MB_ICONWARNING|MB_OK );
    }
}


