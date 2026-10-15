#include "png_read.hpp"

namespace gloview::pixel {

cairo_surface_t *readPngSurface(const std::string &path) {
    auto *surf = cairo_image_surface_create_from_png(path.c_str());
    // cairo does not return null here: it returns a surface in an ERROR state, which is the
    // trap worth closing. Checking the pointer alone would hand back a surface whose width is
    // zero, and the renderer would be asked to upload nothing.
    //
    // The status check is therefore the ONLY gate, and it covers everything at once: no file,
    // an empty path, a directory, junk, a truncated file. An explicit `if (path.empty())` was
    // written and tested, and turned out to be dead -- the test passed with and without it,
    // because cairo answers an empty path with an error surface like anything else. Deleting
    // it is why this comment exists: there is no second guard to keep in step with this one.
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf)
            cairo_surface_destroy(surf);
        return nullptr;
    }
    return surf;
}

} // namespace gloview::pixel
