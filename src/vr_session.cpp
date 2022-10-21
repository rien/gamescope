#include "vr_session.hpp"
#include "main.hpp"
#include "rendervulkan.hpp"
#include "steamcompmgr.hpp"
#include "wlserver.hpp"
#include "log.hpp"
#include <linux/input-event-codes.h>

#include <string.h>
#include <thread>

static LogScope openvr_log("openvr");

static bool GetVulkanInstanceExtensionsRequired( std::vector< std::string > &outInstanceExtensionList );
static bool GetVulkanDeviceExtensionsRequired( VkPhysicalDevice pPhysicalDevice, std::vector< std::string > &outDeviceExtensionList );
static void vrsession_input_thread();

static const char *openvr_getenv(const char *name, const char *def = NULL)
{
    const char *env = getenv(name);
    if (env && *env)
    {
        openvr_log.infof(" - %s: %s", name, env);
        return env;
    }

    openvr_log.infof(" - %s: %s (UNSET)", name, def);
    return def;
}

struct OpenVRSession
{
    const char *pchOverlayKey = nullptr;
    const char *pchOverlayName = nullptr;
    bool bNudgeToVisible = false;
    vr::VROverlayHandle_t hOverlay = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t hOverlayThumbnail = vr::k_ulOverlayHandleInvalid;
};

OpenVRSession &GetVR()
{
    static OpenVRSession s_Global;
    return s_Global;
}

bool vr_init()
{
    vr::EVRInitError error = vr::VRInitError_None;
    VR_Init(&error, vr::VRApplication_Background);

    if ( error != vr::VRInitError_None )
    {
        openvr_log.errorf("Unable to init VR runtime: %s\n",vr::VR_GetVRInitErrorAsEnglishDescription( error ));
        return false;
    }

    return true;
}

// Not in public headers yet.
namespace vr
{
    const VROverlayFlags VROverlayFlags_EnableControlBar = (VROverlayFlags)(1 << 23);
    const VROverlayFlags VROverlayFlags_EnableControlBarKeyboard = (VROverlayFlags)(1 << 24);
    const VROverlayFlags VROverlayFlags_EnableControlBarClose = (VROverlayFlags)(1 << 25);
}

bool vrsession_init()
{
    // Setup the overlay.

    if ( !vr::VROverlay() )
    {
        openvr_log.errorf("SteamVR runtime version mismatch!\n");
        return false;		
    }

    //GetVR().pchOverlayKey = "valve.steam.bigpicture";
    GetVR().pchOverlayKey  = openvr_getenv( "GAMESCOPE_VR_OVERLAY_KEY", "gamescope" );
    GetVR().pchOverlayName = openvr_getenv( "GAMESCOPE_VR_OVERLAY_NAME", "Gamescope VR" );

    vr::VROverlay()->CreateDashboardOverlay(
        GetVR().pchOverlayKey,
        GetVR().pchOverlayName,
        &GetVR().hOverlay, &GetVR().hOverlayThumbnail );

    vr::VROverlay()->SetOverlayInputMethod( GetVR().hOverlay, vr::VROverlayInputMethod_Mouse );

    vr::HmdVector2_t vMouseScale = { { (float)g_nNestedWidth, (float)g_nNestedHeight } };
    vr::VROverlay()->SetOverlayMouseScale( GetVR().hOverlay, &vMouseScale );

    const bool bShowAutomatically           = !!atoi( openvr_getenv( "GAMESCOPE_VR_SHOW_OVERLAY", "1" ) );
    if ( bShowAutomatically )
        GetVR().bNudgeToVisible = bShowAutomatically;

    const bool bHideLaserIntersection       = !!atoi( openvr_getenv( "GAMESCOPE_VR_HIDE_LASER_INTERSECTION",		"0" ) );
    const bool bEnableControlBar            = !!atoi( openvr_getenv( "GAMESCOPE_VR_ENABLE_CONTROL_BAR",				"1" ) );
    const bool bEnableControlBarKeyboard    = !!atoi( openvr_getenv( "GAMESCOPE_VR_ENABLE_CONTROL_BAR_KEYBOARD",	"1" ) );
    const bool bEnableControlBarClose       = !!atoi( openvr_getenv( "GAMESCOPE_VR_ENABLE_CONTROL_BAR_CLOSE",		"1" ) );
    const bool bWantsModalBehaviour         = !!atoi( openvr_getenv( "GAMESCOPE_VR_MODAL",					        "0" ) );
    vr::VROverlay()->SetOverlayFlag( GetVR().hOverlay, vr::VROverlayFlags_HideLaserIntersection,	bHideLaserIntersection );
    vr::VROverlay()->SetOverlayFlag( GetVR().hOverlay, vr::VROverlayFlags_IgnoreTextureAlpha,		true );
    vr::VROverlay()->SetOverlayFlag( GetVR().hOverlay, vr::VROverlayFlags_EnableControlBar,			bEnableControlBar );
    vr::VROverlay()->SetOverlayFlag( GetVR().hOverlay, vr::VROverlayFlags_EnableControlBarKeyboard,	bEnableControlBarKeyboard );
    vr::VROverlay()->SetOverlayFlag( GetVR().hOverlay, vr::VROverlayFlags_EnableControlBarClose,	bEnableControlBarClose );
    vr::VROverlay()->SetOverlayFlag( GetVR().hOverlay, vr::VROverlayFlags_WantsModalBehavior,	    bWantsModalBehaviour );

    const float flOverlayWidth              = atof( openvr_getenv( "GAMESCOPE_VR_OVERLAY_WIDTH_METRES",		"2.0" ) );
    const float flOverlayCurvature          = atof( openvr_getenv( "GAMESCOPE_VR_OVERLAY_CURVATURE",		"0.0" ) );
    const float flOverlayPreCurvePitch      = atof( openvr_getenv( "GAMESCOPE_VR_OVERLAY_PRE_CURVE_PITCH",	"0.0" ) );
    vr::VROverlay()->SetOverlayWidthInMeters( GetVR().hOverlay,  flOverlayWidth );
    vr::VROverlay()->SetOverlayCurvature	( GetVR().hOverlay,  flOverlayCurvature );
    vr::VROverlay()->SetOverlayPreCurvePitch( GetVR().hOverlay,  flOverlayPreCurvePitch );

    const char *pchEnvOverlayIcon = openvr_getenv( "GAMESCOPE_VR_OVERLAY_ICON" );
    if ( pchEnvOverlayIcon )
    {
        vr::EVROverlayError err = vr::VROverlay()->SetOverlayFromFile( GetVR().hOverlayThumbnail, pchEnvOverlayIcon );
        if( err != vr::VROverlayError_None )
        {
            openvr_log.errorf( "Unable to set thumbnail to %s: %s\n", pchEnvOverlayIcon, vr::VROverlay()->GetOverlayErrorNameFromEnum( err ) );
        }
    }

    // Setup misc. stuff

    g_nOutputRefresh = (int) vr::VRSystem()->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float );

    std::thread input_thread_vrinput( vrsession_input_thread );
    input_thread_vrinput.detach();

    return true;
}

bool vrsession_visible()
{
    return vr::VROverlay()->IsOverlayVisible( GetVR().hOverlay ) || GetVR().bNudgeToVisible;
}

void vrsession_present( vr::VRVulkanTextureData_t *pTextureData )
{
    vr::Texture_t texture = { pTextureData, vr::TextureType_Vulkan, vr::ColorSpace_Gamma };
    vr::VROverlay()->SetOverlayTexture( GetVR().hOverlay, &texture );
    if ( GetVR().bNudgeToVisible )
    {
        vr::VROverlay()->ShowDashboard( GetVR().pchOverlayKey );
        GetVR().bNudgeToVisible = false;
    }
}

static void vector_append_unique_str( std::vector<const char *>& exts, const char *str )
{
    for ( auto &c_str : exts )
    {
        if ( !strcmp( c_str, str ) )
            return;
    }

    exts.push_back( str );
}

void vrsession_append_instance_exts( std::vector<const char *>& exts )
{
    static std::vector<std::string> s_exts;
    GetVulkanInstanceExtensionsRequired( s_exts );

    for (const auto &str : s_exts)
        vector_append_unique_str( exts, str.c_str() );
}

void vrsession_append_device_exts( VkPhysicalDevice physDev, std::vector<const char *>& exts )
{
    static std::vector<std::string> s_exts;
    GetVulkanDeviceExtensionsRequired( physDev, s_exts );

    for (const auto &str : s_exts)
        vector_append_unique_str( exts, str.c_str() );
}

bool vrsession_framesync( uint32_t timeoutMS )
{
    return vr::VROverlay()->WaitFrameSync( timeoutMS ) != vr::VROverlayError_None;
}

static int VRButtonToWLButton( vr::EVRMouseButton mb )
{
    switch( mb )
    {
        default:
        case vr::VRMouseButton_Left:
            return BTN_LEFT;
        case vr::VRMouseButton_Right:
            return BTN_RIGHT;
        case vr::VRMouseButton_Middle:
            return BTN_MIDDLE;
    }
}

static void vrsession_input_thread()
{
    pthread_setname_np( pthread_self(), "gamescope-vrinp" );

    // Josh: PollNextOverlayEvent sucks.
    // I want WaitNextOverlayEvent (like SDL_WaitEvent) so this doesn't have to spin and sleep.
    while (true)
    {
        vr::VREvent_t vrEvent;
        while( vr::VROverlay()->PollNextOverlayEvent( GetVR().hOverlay, &vrEvent, sizeof( vrEvent ) ) )
        {
            uint32_t timestamp = vrEvent.eventAgeSeconds * 1'000'000;

            switch( vrEvent.eventType )
            {
                case vr::VREvent_OverlayClosed:
                case vr::VREvent_Quit:
                    g_bRun = false;
                    nudge_steamcompmgr();
                    break;

                case vr::VREvent_KeyboardCharInput:
                    // I need to deal with a buuuunch of stuff about mapping keyboards here.
                    // I will come back to this.
                    openvr_log.errorf("VREvent_KeyboardCharInput: Not implemented.");
                    break;

                case vr::VREvent_MouseMove:
                    wlserver_lock();
                    wlserver_mousewarp( vrEvent.data.mouse.x, g_nNestedHeight - vrEvent.data.mouse.y, timestamp );
                    wlserver_unlock();
                    break;

                case vr::VREvent_MouseButtonUp:
                case vr::VREvent_MouseButtonDown:
                    wlserver_lock();
                    wlserver_mousebutton( VRButtonToWLButton( (vr::EVRMouseButton) vrEvent.data.mouse.button ),
                                        vrEvent.eventType == vr::VREvent_MouseButtonDown,
                                        timestamp );
                    wlserver_unlock();
                    break;
            }
        }
        sleep_for_nanos(2'000'000);
    }
}

///////////////////////////////////////////////
// Josh:
// GetVulkanInstanceExtensionsRequired and GetVulkanDeviceExtensionsRequired return *space separated* exts :(
// I am too lazy to write that myself.
// This is stolen verbatim from hellovr_vulkan with the .clear removed.
// If it is broken, blame the samples.

static bool GetVulkanInstanceExtensionsRequired( std::vector< std::string > &outInstanceExtensionList )
{
    if ( !vr::VRCompositor() )
    {
        return false;
    }

    uint32_t nBufferSize = vr::VRCompositor()->GetVulkanInstanceExtensionsRequired( nullptr, 0 );
    if ( nBufferSize > 0 )
    {
        // Allocate memory for the space separated list and query for it
        char *pExtensionStr = new char[ nBufferSize ];
        pExtensionStr[0] = 0;
        vr::VRCompositor()->GetVulkanInstanceExtensionsRequired( pExtensionStr, nBufferSize );

        // Break up the space separated list into entries on the CUtlStringList
        std::string curExtStr;
        uint32_t nIndex = 0;
        while ( pExtensionStr[ nIndex ] != 0 && ( nIndex < nBufferSize ) )
        {
            if ( pExtensionStr[ nIndex ] == ' ' )
            {
                outInstanceExtensionList.push_back( curExtStr );
                curExtStr.clear();
            }
            else
            {
                curExtStr += pExtensionStr[ nIndex ];
            }
            nIndex++;
        }
        if ( curExtStr.size() > 0 )
        {
            outInstanceExtensionList.push_back( curExtStr );
        }

        delete [] pExtensionStr;
    }

    return true;
}

static bool GetVulkanDeviceExtensionsRequired( VkPhysicalDevice pPhysicalDevice, std::vector< std::string > &outDeviceExtensionList )
{
    if ( !vr::VRCompositor() )
    {
        return false;
    }

    uint32_t nBufferSize = vr::VRCompositor()->GetVulkanDeviceExtensionsRequired( ( VkPhysicalDevice_T * ) pPhysicalDevice, nullptr, 0 );
    if ( nBufferSize > 0 )
    {
        // Allocate memory for the space separated list and query for it
        char *pExtensionStr = new char[ nBufferSize ];
        pExtensionStr[0] = 0;
        vr::VRCompositor()->GetVulkanDeviceExtensionsRequired( ( VkPhysicalDevice_T * ) pPhysicalDevice, pExtensionStr, nBufferSize );

        // Break up the space separated list into entries on the CUtlStringList
        std::string curExtStr;
        uint32_t nIndex = 0;
        while ( pExtensionStr[ nIndex ] != 0 && ( nIndex < nBufferSize ) )
        {
            if ( pExtensionStr[ nIndex ] == ' ' )
            {
                outDeviceExtensionList.push_back( curExtStr );
                curExtStr.clear();
            }
            else
            {
                curExtStr += pExtensionStr[ nIndex ];
            }
            nIndex++;
        }
        if ( curExtStr.size() > 0 )
        {
            outDeviceExtensionList.push_back( curExtStr );
        }

        delete [] pExtensionStr;
    }

    return true;
}
