#include "png_read.hpp"

namespace gloview::pixel {

cairo_surface_t *readPngSurface(const std::string &path) {
    auto *surf = cairo_image_surface_create_from_png(path.c_str());
    // cairo returns an ERROR surface, not null: the status check is the ONLY gate, covering no
    // file, an empty path, a directory, junk and a truncated file alike.
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf)
            cairo_surface_destroy(surf);
        return nullptr;
    }
    return surf;
}

} // namespace gloview::pixel
