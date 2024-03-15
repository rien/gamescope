#include <X11/extensions/XTest.h>
#include <wayland-client.h>
#include "../src/log.hpp"

#include "gamescope-xtest-client-protocol.h"

static LogScope xtest_log("xtest_gamescope");

static wl_display *GetDisplay()
{
    static wl_display *s_pDisplay = []() -> wl_display *
    {
        const char *pWaylandDisplay = getenv( "GAMESCOPE_WAYLAND_DISPLAY" );
        if ( !pWaylandDisplay || !*pWaylandDisplay )
            return nullptr;

        wl_display *pDisplay = wl_display_connect( pWaylandDisplay );
        if ( !pDisplay )
            return nullptr;

        return pDisplay;
    }();

    return s_pDisplay;
}

static gamescope_xtest *GetXTestInterface()
{
    static gamescope_xtest *s_pInterface = []() -> gamescope_xtest *
    {
        wl_display *pDisplay = GetDisplay();
        if ( !pDisplay )
            return nullptr;

        wl_registry *pRegistry = wl_display_get_registry( pDisplay );
        if ( !pRegistry )
            return nullptr;

        gamescope_xtest *pGamescopeXTest = nullptr;
        static constexpr wl_registry_listener s_RegistryListener =
        {
            .global = []( void *pData, wl_registry *pRegistry, uint32_t uName, const char *pInterface, uint32_t uVersion )
            {
                // Sanity...
                if ( !pInterface || !pRegistry || !pData )
                    return;

                gamescope_xtest **pOutXTest = (gamescope_xtest **)pData;
                if ( !strcmp( pInterface, gamescope_xtest_interface.name ) && uVersion == 1 )
                {
                    *pOutXTest = (gamescope_xtest *)wl_registry_bind( pRegistry, uName, &gamescope_xtest_interface, 1u );
                }
            },
            .global_remove = []( void *pData, wl_registry *pRegistry, uint32_t uName )
            {
            }
        };
        wl_registry_add_listener( pRegistry, &s_RegistryListener, &pGamescopeXTest );
        wl_display_roundtrip( pDisplay );

        wl_registry_destroy( pRegistry );
        pRegistry = nullptr;

        if ( !pGamescopeXTest )
            return nullptr;

        return pGamescopeXTest;
    }();

    return s_pInterface;
}

extern "C"
{
    Bool XTestCompareCursorWithWindow(
        Display*		/* dpy */,
        Window		/* window */,
        Cursor		/* cursor */
    )
    {
        xtest_log.infof( "XTestCompareCursorWithWindow" );
        return False;
    }

    Bool XTestCompareCurrentCursorWithWindow(
        Display*		/* dpy */,
        Window		/* window */
    )
    {
        xtest_log.infof( "XTestCompareCurrentCursorWithWindow" );
        return False;
    }

    int XTestFakeKeyEvent(
        Display*		/* dpy */,
        unsigned int	/* keycode */,
        Bool		/* is_press */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeKeyEvent" );
        return 0;
    }

    int XTestFakeButtonEvent(
        Display*		/* dpy */,
        unsigned int	/* button */,
        Bool		/* is_press */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeButtonEvent" );
        return 0;
    }

    int XTestFakeMotionEvent(
        Display*		/* dpy */,
        int			/* screen */,
        int			/* x */,
        int			/* y */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeMotionEvent" );
        return 0;
    }

    int XTestFakeRelativeMotionEvent(
        Display*        pXDisplay,
        int             nX,
        int             nY,
        unsigned long   ulDelay
    )
    {
        xtest_log.infof( "XTestFakeRelativeMotionEvent" );

        wl_display *pDisplay = GetDisplay();
        gamescope_xtest *pXTest = GetXTestInterface();
        if ( !pDisplay || !pXTest )
            return 0;

        gamescope_xtest_relative_motion( pXTest, wl_fixed_from_int( nX ), wl_fixed_from_int( nY ) );
        wl_display_flush( pDisplay );

        return 0;
    }

    int XTestFakeDeviceKeyEvent(
        Display*		/* dpy */,
        XDevice*		/* dev */,
        unsigned int	/* keycode */,
        Bool		/* is_press */,
        int*		/* axes */,
        int			/* n_axes */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeDeviceKeyEvent" );
        return 0;
    }

    int XTestFakeDeviceButtonEvent(
        Display*		/* dpy */,
        XDevice*		/* dev */,
        unsigned int	/* button */,
        Bool		/* is_press */,
        int*		/* axes */,
        int			/* n_axes */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeDeviceButtonEvent" );
        return 0;
    }

    int XTestFakeProximityEvent(
        Display*		/* dpy */,
        XDevice*		/* dev */,
        Bool		/* in_prox */,
        int*		/* axes */,
        int			/* n_axes */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeProximityEvent" );
        return 0;
    }

    int XTestFakeDeviceMotionEvent(
        Display*		/* dpy */,
        XDevice*		/* dev */,
        Bool		/* is_relative */,
        int			/* first_axis */,
        int*		/* axes */,
        int			/* n_axes */,
        unsigned long	/* delay */
    )
    {
        xtest_log.infof( "XTestFakeDeviceMotionEvent" );
        return 0;
    }

    int XTestGrabControl(
        Display*		/* dpy */,
        Bool		/* impervious */
    )
    {
        xtest_log.infof( "XTestGrabControl" );
        return 0;
    }

    void XTestSetGContextOfGC(
        GC			/* gc */,
        GContext		/* gid */
    )
    {
        xtest_log.infof( "XTestSetGContextOfGC" );
    }

    void XTestSetVisualIDOfVisual(
        Visual*		/* visual */,
        VisualID		/* visualid */
    )
    {
        xtest_log.infof( "XTestSetVisualIDOfVisual" );
    }

    Status XTestDiscard(
        Display*		/* dpy */
    )
    {
        xtest_log.infof( "XTestDiscard" );
        return Success;
    }
}