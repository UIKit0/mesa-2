#include <stdio.h>

#include "target-helpers/inline_debug_helper.h"
#include "tegra/drm/tegra_drm_public.h"
#include "state_tracker/drm_driver.h"

static struct pipe_screen *create_screen(int fd)
{
	struct pipe_screen *screen;

	fprintf(stderr, "> %s(fd=%d)\n", __func__, fd);

	screen = tegra_drm_screen_create(fd);
	if (!screen)
		goto out;

	screen = debug_screen_wrap(screen);

out:
	fprintf(stderr, "< %s() = %p\n", __func__, screen);
	return screen;
}

DRM_DRIVER_DESCRIPTOR("tegra", "tegra", create_screen, NULL)
