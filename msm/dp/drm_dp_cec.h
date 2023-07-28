/*
 * Copyright © 2008 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <drm/drm_connector.h>

#ifdef CONFIG_DRM_DP_CEC
void drm_dp_cec_irq(struct drm_dp_aux *aux);
void drm_dp_cec_register_connector(struct drm_dp_aux *aux,
				   struct drm_connector *connector);
void drm_dp_cec_unregister_connector(struct drm_dp_aux *aux);
void drm_dp_cec_set_edid(struct drm_dp_aux *aux, const struct edid *edid);
void drm_dp_cec_unset_edid(struct drm_dp_aux *aux);
#else
static inline void drm_dp_cec_irq(struct drm_dp_aux *aux)
{
}

static inline void
drm_dp_cec_register_connector(struct drm_dp_aux *aux,
			      struct drm_connector *connector)
{
}

static inline void drm_dp_cec_unregister_connector(struct drm_dp_aux *aux)
{
}

static inline void drm_dp_cec_set_edid(struct drm_dp_aux *aux,
				       const struct edid *edid)
{
}

static inline void drm_dp_cec_unset_edid(struct drm_dp_aux *aux)
{
}

#endif

