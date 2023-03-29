#include <wayland-client.h>
#include "drm-lease-v1-client-protocol.h"

#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <float.h>
#include <string.h>

#include "drm.hpp"

extern const char *g_pOriginalWaylandDisplay;

struct wayland_state;

struct lease_device {
    int drm_fd;
    char *path;
    struct wp_drm_lease_device_v1 *device;
    struct lease_device *next;
    struct wayland_state *state;
};

struct wayland_state {
    int nb_devices;
    struct lease_device *devices;
    struct wp_drm_lease_connector_v1 *connector;
    struct wp_drm_lease_device_v1 *device;
};

static void
lease_fd(void *data, struct wp_drm_lease_v1 *lease, int32_t leased_fd)
{
    printf("lease fd %d\n", leased_fd);
    int *fd = (int *)data;
    *fd = leased_fd;
}

static void
lease_finished(void *data, struct wp_drm_lease_v1 *lease)
{
    int *fd = (int *)data;
    printf("finishing lease fd %d\n", *fd);
    close(*fd);
    *fd = -1;
}

static const struct wp_drm_lease_v1_listener lease_listener = {
    .lease_fd = lease_fd,
    .finished = lease_finished,
};

static void
lease_connector_name(void *data, struct wp_drm_lease_connector_v1 *conn,
        const char *name)
{
    printf("lease connector name %s\n", name);
    struct lease_device *dev = (struct lease_device *)data;
    printf("on path %s\n", dev->path);
}

static void
lease_connector_desc(void *data, struct wp_drm_lease_connector_v1 *conn,
        const char *desc)
{
    printf("lease connector description %s\n", desc);
    struct lease_device *dev = (struct lease_device *)data;
    printf("on path %s\n", dev->path);
}

static void
lease_connector_id(void *data, struct wp_drm_lease_connector_v1 *conn,
        uint32_t id)
{
    printf("lease connector id %zu\n", (size_t)id);
    struct lease_device *dev = (struct lease_device *)data;
    printf("on path %s\n", dev->path);

    drmModeConnector *drm_conn = drmModeGetConnector(dev->drm_fd, id);
    if (!drm_conn) {
        printf("failed to get drm connector : %s (%d)\n", strerror(errno), (errno));
        exit(EXIT_FAILURE);
    }
}

static void lease_connector_done(void *data, struct wp_drm_lease_connector_v1 *conn)
{
    printf("lease connector done\n");
}

static void
lease_connector_withdrawn(void *data, struct wp_drm_lease_connector_v1 *conn)
{
    printf("lease connector withdrawn\n");
    struct lease_device *dev = (struct lease_device *)data;
    printf("on path %s\n", dev->path);
}

static const struct wp_drm_lease_connector_v1_listener lease_connector_listener = {
    .name = lease_connector_name,
    .description = lease_connector_desc,
    .connector_id = lease_connector_id,
    .done = lease_connector_done,
    .withdrawn = lease_connector_withdrawn,
};

static void
lease_device_drm_fd(void *data, struct wp_drm_lease_device_v1 *device, int32_t fd)
{
    struct lease_device *dev = (struct lease_device *)data;
    dev->path = drmGetDeviceNameFromFd2(fd);
    printf("lease device drm_fd %s\n", dev->path);

    dev->drm_fd = fd;

    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        fprintf(stderr, "drmModeGetResources failed\n");
        exit(EXIT_FAILURE);
    }

    printf("\tcount crtcs: %d\n", res->count_crtcs);
    for (int i = 0; i < res->count_crtcs; ++i) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        printf("\t\tcrtc id %d\n", crtc->crtc_id);
        drmModeFreeCrtc(crtc);
    }

    printf("\tcount_connectors: %d\n", res->count_connectors);
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnectorPtr conn = drmModeGetConnector(
                fd, res->connectors[i]);
        printf("\t\tconn id %d: %d modes\n", conn->connector_id,
                conn->count_modes);
        drmModeFreeConnector(conn);
    }

    printf("\tcount encoders: %d\n", res->count_encoders);
    for (int i = 0; i < res->count_encoders; ++i) {
        drmModeEncoderPtr encoder = drmModeGetEncoder(fd, res->encoders[i]);
        printf("\t\tencoder id %d\n", encoder->encoder_id);
        drmModeFreeEncoder(encoder);
    }
    drmModeFreeResources(res);
}

static void
lease_device_connector(void *data, struct wp_drm_lease_device_v1 *device,
        struct wp_drm_lease_connector_v1 *conn)
{
    struct lease_device *dev = (struct lease_device *)data;
    printf("lease device connector %s\n", dev->path);
    if (!dev->state->connector) {
        dev->state->connector = conn;
        dev->state->device = dev->device;
        wp_drm_lease_connector_v1_add_listener(dev->state->connector,
                &lease_connector_listener, dev);
    } else {
        printf("device already has a connector\n");
    }
}

static void
lease_device_done(void *data, struct wp_drm_lease_device_v1 *dev)
{
    printf("lease device done\n");
}

static void
lease_device_released(void *data, struct wp_drm_lease_device_v1 *dev)
{
    printf("lease device released\n");
}

static const struct wp_drm_lease_device_v1_listener lease_device_listener = {
    .drm_fd = lease_device_drm_fd,
    .connector = lease_device_connector,
    .done = lease_device_done,
    .released = lease_device_released,
};

static void
registry_global(void *data, struct wl_registry *wl_registry, uint32_t name,
        const char *interface, uint32_t version)
{
    struct wayland_state *state = (struct wayland_state *)data;
    if (strcmp(interface, wp_drm_lease_device_v1_interface.name) == 0) {
        printf("interface %s detected\n", interface);
        struct lease_device *dev = (struct lease_device *)calloc(1, sizeof(struct lease_device));
        assert(dev);
        state->nb_devices++;
        dev->device = (struct wp_drm_lease_device_v1 *)wl_registry_bind(wl_registry, name,
                &wp_drm_lease_device_v1_interface, 1);
        wp_drm_lease_device_v1_add_listener(dev->device,
                &lease_device_listener, dev);

        dev->state = state;

        struct lease_device **link = &state->devices;
        while (*link) {
            link = &(*link)->next;
        }
        *link = dev;
    }
}

static void
registry_global_remove(void *data, struct wl_registry *wl_registry,
        uint32_t name)
{
    // unused
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int init_drm_lease()
{
    if (!g_pOriginalWaylandDisplay)
        return -1;

    struct wl_display *display = wl_display_connect( g_pOriginalWaylandDisplay );
    if (!display)
    {
        printf("No wayland display to try to lease.\n");
        return -1;
    }

    struct wayland_state state = {};

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, (void*)&state);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (!state.connector)
    {
        printf("No connector to lease.\n");
        return -1;
    }

    printf("state has %d devices\n", state.nb_devices);

    struct wp_drm_lease_request_v1 *request =
            wp_drm_lease_device_v1_create_lease_request(state.device);
    if (!request)
    {
        printf("Failed to create lease request.\n");
        return -1;
    }

    wp_drm_lease_request_v1_request_connector(request,
            state.connector);

    struct wp_drm_lease_v1 *lease =
        wp_drm_lease_request_v1_submit(request);
    if (!lease)
    {
        printf("Failed to create lease.\n");
        return -1;
    }

    int fd = -1;
    wp_drm_lease_v1_add_listener(lease, &lease_listener, &fd);

    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    sleep(2);

    return fd;
}
