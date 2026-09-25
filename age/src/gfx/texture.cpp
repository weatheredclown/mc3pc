#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include "gfx/texture.h"
#include "gfx/texpal.h"
#include "gfx/loadimg.h"
#include "gfx/bitmap.h"
#include "data/assetcfg.h"
#include "core/stream.h"
#include "core/output.h"
#include <map>
#include "atl/array.h"
#include <algorithm>
#include "gfx/misc.h"
#include "gfx/simple.h"
#include "gfx/font.h"
#include "data/memory.h"
#include "data/resource.h"
#include "rmcore/rscgeom.h"
#include "data/args.h"

// Direct3D 11 Device and Context Getters (defined in rgl.cpp)
extern "C" ID3D11Device* gfxGetDevice();
extern "C" ID3D11DeviceContext* gfxGetContext();

static std::map<std::string, gfxTexture*> sTextureCache;

static std::string TrimTextureName(const std::string &name) {
    std::string trimmed = name;
    // Trim leading and trailing whitespace
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first != std::string::npos) {
        size_t last = trimmed.find_last_not_of(" \t\r\n");
        trimmed = trimmed.substr(first, (last - first + 1));
    } else {
        trimmed = "";
    }
    // Lowercase
    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), ::tolower);
    // Remove .tex or .tex.highres or .highres
    size_t pos;
    while ((pos = trimmed.find(".tex.highres")) != std::string::npos) {
        trimmed.replace(pos, 12, "");
    }
    while ((pos = trimmed.find(".tex")) != std::string::npos) {
        trimmed.replace(pos, 4, "");
    }
    while ((pos = trimmed.find(".highres")) != std::string::npos) {
        trimmed.replace(pos, 8, "");
    }
    return trimmed;
}

gfxTexture *gfxGetTexture(const char *name, bool mipmaps, bool silent) {
    if (!name || name[0] == '\0' || _stricmp(name, "none") == 0) {
        return NULL;
    }

    std::string key = TrimTextureName(name);
    auto it = sTextureCache.find(key);
    if (it != sTextureCache.end()) {
        if (it->second) {
            it->second->AddRef();   // one reference per successful Get
        }
        return it->second;
    }
    // Texture allocations tag the bucket the game selected (gfxMemory).
    datUseMemoryBucket bucketScope(gfxMemory::GetTextureBucket());

    // Try loading texture file. The high-res variant is optional, so probe for
    // it with Exists() rather than Open() -- that way a missing .tex.highres
    // never logs a failure. Only the base .tex is required; a miss there falls
    // through to the Errorf below.
    auto probeTextureKey = [](const std::string &k) -> Stream* {
        Stream *st = NULL;
        static const char *exts[] = {
            "tex.highres", "tex",
            "tga.highres", "tga",
            "png.highres", "png",
            "bmp"
        };
        for (const char *ext : exts) {
            if (ASSET.Exists(k.c_str(), ext)) {
                st = ASSET.Open(k.c_str(), ext);
                if (st) return st;
            }
            std::string origKey = k + "_orig";
            if (ASSET.Exists(origKey.c_str(), ext)) {
                st = ASSET.Open(origKey.c_str(), ext);
                if (st) return st;
            }
        }
        return NULL;
    };

    Stream *s = probeTextureKey(key);
    if (!s && gfxLoadImageFolder && gfxLoadImageFolder[0]) {
        s = probeTextureKey(std::string(gfxLoadImageFolder) + "/" + key);
    }
    if (!s) {
        s = probeTextureKey("texture/" + key);
    }
    if (!s) {
        // MC3 vehicle textures named by the car packs' shader tables (suspension_00, grill_00, ...)
        s = probeTextureKey("$/vehicle/shared_texture/texture/" + key);
    }
    if (!s) {
        s = probeTextureKey("hud/" + key);
    }
    if (!s) {
        std::string altKey = key;
        std::string lowerAlt = altKey;
        std::transform(lowerAlt.begin(), lowerAlt.end(), lowerAlt.begin(), ::tolower);
        size_t pos = lowerAlt.find("sciglasses");
        if (pos != std::string::npos) {
            altKey.replace(pos, 10, "sci");
            s = probeTextureKey(altKey);
        }
    }
    if (!s) {
        s = probeTextureKey("entity/sci/" + key);
    }

    if (!s) {
        if (!silent) {
            Errorf("gfxGetTexture: failed to open texture '%s'", name);
        }
        // Negative-cache the miss: callers ask for this texture every frame, so
        // without recording the failure we'd re-open the file and re-log the
        // error indefinitely. A cached NULL is returned silently from now on.
        sTextureCache[key] = NULL;
        return NULL;
    }

    // Read file contents
    atArray<uint8_t> fileData;
    int fileSize = s->Size();
    if (fileSize > 0) {
        fileData.Resize(fileSize);
        int totalRead = 0;
        while (totalRead < fileSize) {
            int r = s->Read(fileData.data() + totalRead, fileSize - totalRead);
            if (r <= 0) break;
            totalRead += r;
        }
        fileData.Resize(totalRead);
    } else {
        uint8_t buf[4096];
        while (true) {
            int r = s->Read(buf, sizeof(buf));
            if (r <= 0) break;
            int cur = fileData.GetCount();
            fileData.Resize(cur + r);
            memcpy(fileData.data() + cur, buf, r);
        }
    }
    s->Close();

    if (fileData.GetCount() < 14) {
        Errorf("gfxGetTexture: texture '%s' too small (%d bytes)", name, fileData.GetCount());
        sTextureCache[key] = NULL;
        return NULL;
    }

    // Check for TGA format
    bool isTGA = false;
    uint16_t tga_w = 0, tga_h = 0;
    uint8_t tga_bpp = 0, tga_descriptor = 0, tga_img_type = 0, tga_id_len = 0;
    if (fileData.GetCount() >= 18) {
        tga_id_len = fileData[0];
        uint8_t cm_type = fileData[1];
        tga_img_type = fileData[2];
        if ((cm_type == 0 || cm_type == 1) && (tga_img_type == 2 || tga_img_type == 3 || tga_img_type == 10 || tga_img_type == 11)) {
            tga_w = fileData[12] | (fileData[13] << 8);
            tga_h = fileData[14] | (fileData[15] << 8);
            tga_bpp = fileData[16];
            tga_descriptor = fileData[17];
            if (tga_w > 0 && tga_w <= 4096 && tga_h > 0 && tga_h <= 4096 && (tga_bpp == 8 || tga_bpp == 24 || tga_bpp == 32)) {
                isTGA = true;
            }
        }
    }

    uint32_t width = isTGA ? tga_w : (fileData[0] | (fileData[1] << 8));
    uint32_t height = isTGA ? tga_h : (fileData[2] | (fileData[3] << 8));
    uint16_t tex_type = isTGA ? 0 : (fileData[4] | (fileData[5] << 8));

    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        Errorf("gfxGetTexture: invalid texture dimensions %dx%d for '%s'", width, height, name);
        sTextureCache[key] = NULL;
        return NULL;
    }

    atArray<uint8_t> rgba;
    rgba.Resize(width * height * 4);
    memset(rgba.data(), 0, width * height * 4);
    bool parsed = false;
    bool paletted8 = false;   // 8-bit indexed source: keep indices + CLUT for recolouring
    size_t palIdxOff = 0;     // offset of the index map in the file
    bool palHasClut = false;  // CLUT embedded at 0x0E (else the effect supplies one)

    if (isTGA) {
        size_t src_off = 18 + tga_id_len;
        size_t total_pixels = width * height;
        size_t bytesPerPixel = tga_bpp / 8;

        atArray<uint8_t> decompressedPixels;
        const uint8_t *pixelSrc = NULL;

        if (tga_img_type == 10 || tga_img_type == 11) { // RLE
            decompressedPixels.Resize(total_pixels * bytesPerPixel);
            size_t pxCount = 0;
            size_t off = src_off;
            while (pxCount < total_pixels && off < (size_t)fileData.GetCount()) {
                uint8_t header = fileData[off++];
                size_t count = (header & 0x7F) + 1;
                if (header & 0x80) { // RLE run
                    if (off + bytesPerPixel > (size_t)fileData.GetCount()) break;
                    for (size_t i = 0; i < count && pxCount < total_pixels; ++i) {
                        memcpy(&decompressedPixels[pxCount * bytesPerPixel], &fileData[off], bytesPerPixel);
                        pxCount++;
                    }
                    off += bytesPerPixel;
                } else { // Raw run
                    for (size_t i = 0; i < count && pxCount < total_pixels; ++i) {
                        if (off + bytesPerPixel > (size_t)fileData.GetCount()) break;
                        memcpy(&decompressedPixels[pxCount * bytesPerPixel], &fileData[off], bytesPerPixel);
                        off += bytesPerPixel;
                        pxCount++;
                    }
                }
            }
            pixelSrc = decompressedPixels.data();
        } else {
            pixelSrc = &fileData[src_off];
        }

        bool topDown = (tga_descriptor & 0x20) != 0;
        if (pixelSrc && src_off + total_pixels * bytesPerPixel <= (size_t)fileData.GetCount() || (tga_img_type == 10 || tga_img_type == 11)) {
            for (uint32_t y = 0; y < height; ++y) {
                uint32_t srcY = topDown ? y : (height - 1 - y);
                for (uint32_t x = 0; x < width; ++x) {
                    size_t srcIdx = (srcY * width + x) * bytesPerPixel;
                    size_t dstIdx = (y * width + x) * 4;
                    if (tga_bpp == 32) {
                        rgba[dstIdx]     = pixelSrc[srcIdx + 2]; // R
                        rgba[dstIdx + 1] = pixelSrc[srcIdx + 1]; // G
                        rgba[dstIdx + 2] = pixelSrc[srcIdx];     // B
                        rgba[dstIdx + 3] = pixelSrc[srcIdx + 3]; // A
                    } else if (tga_bpp == 24) {
                        rgba[dstIdx]     = pixelSrc[srcIdx + 2]; // R
                        rgba[dstIdx + 1] = pixelSrc[srcIdx + 1]; // G
                        rgba[dstIdx + 2] = pixelSrc[srcIdx];     // B
                        rgba[dstIdx + 3] = 0xFF;                 // A
                    } else if (tga_bpp == 8) {
                        uint8_t v = pixelSrc[srcIdx];
                        rgba[dstIdx]     = v;
                        rgba[dstIdx + 1] = v;
                        rgba[dstIdx + 2] = v;
                        rgba[dstIdx + 3] = 0xFF;
                    }
                }
            }
            parsed = true;
        }
    } else if (tex_type == 0x01 || tex_type == 0x0E) {
        // 8-bit indexed with BGRA palette (256 entries at offset 0x0E)
        if (fileData.GetCount() >= 0x40E + (width * height)) {
            size_t clut_off = 0x0E;
            size_t data_off = 0x40E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    size_t src = data_off + (y * width + x);
                    uint8_t idx = fileData[src];
                    size_t c_off = clut_off + idx * 4;
                    size_t dst = (y * width + x) * 4;
                    rgba[dst]     = fileData[c_off + 2]; // R
                    rgba[dst + 1] = fileData[c_off + 1]; // G
                    rgba[dst + 2] = fileData[c_off];     // B
                    rgba[dst + 3] = fileData[c_off + 3]; // A
                }
            }
            parsed = true;
            paletted8 = true; palIdxOff = 0x40E; palHasClut = true;
        }
    } else if (tex_type == 0x0F || tex_type == 0x10) {
        // 4-bit indexed with BGRA palette (16 entries at offset 0x0E)
        if (fileData.GetCount() >= 0x4E + (width * height / 2)) {
            size_t clut_off = 0x0E;
            size_t data_off = 0x4E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; x += 2) {
                    size_t src = data_off + (y * width / 2 + x / 2);
                    uint8_t p = fileData[src];
                    for (uint32_t nibble = 0; nibble < 2; ++nibble) {
                        uint8_t idx = (nibble == 0) ? (p & 0xF) : ((p >> 4) & 0xF);
                        size_t c_off = clut_off + idx * 4;
                        size_t dst = (y * width + x + nibble) * 4;
                        rgba[dst]     = fileData[c_off + 2];
                        rgba[dst + 1] = fileData[c_off + 1];
                        rgba[dst + 2] = fileData[c_off];
                        rgba[dst + 3] = fileData[c_off + 3];
                    }
                }
            }
            parsed = true;
        }
    } else if (tex_type == 0x11) {
        // RGB888
        if (fileData.GetCount() >= 0x0E + (width * height * 3)) {
            size_t data_off = 0x0E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    size_t src = data_off + (y * width + x) * 3;
                    size_t dst = (y * width + x) * 4;
                    rgba[dst]     = fileData[src];
                    rgba[dst + 1] = fileData[src + 1];
                    rgba[dst + 2] = fileData[src + 2];
                    rgba[dst + 3] = 0xFF;
                }
            }
            parsed = true;
        }
    } else if (tex_type == 0x12) {
        // RGBA8888
        if (fileData.GetCount() >= 0x0E + (width * height * 4)) {
            size_t data_off = 0x0E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    size_t src = data_off + (y * width + x) * 4;
                    size_t dst = (y * width + x) * 4;
                    rgba[dst]     = fileData[src];
                    rgba[dst + 1] = fileData[src + 1];
                    rgba[dst + 2] = fileData[src + 2];
                    rgba[dst + 3] = fileData[src + 3];
                }
            }
            parsed = true;
        }
    } else if (tex_type == 0x08) {
        // L8 -- 8-bit luminance/intensity, no palette (data at 0x0E). Used by the
        // GunFx glow sprites (corona, hit_pulse): the byte is the glow
        // brightness, expanded to grayscale RGB with matching alpha so it
        // composites correctly under both additive and alpha blending. (Distinct
        // from 0x0B/A8, which forces white RGB so the FX layer can tint it.)
        if (fileData.GetCount() >= 0x0E + (width * height)) {
            size_t data_off = 0x0E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    size_t src = data_off + (y * width + x);
                    uint8_t v = fileData[src];
                    size_t dst = (y * width + x) * 4;
                    rgba[dst]     = v;
                    rgba[dst + 1] = v;
                    rgba[dst + 2] = v;
                    rgba[dst + 3] = v;
                }
            }
            parsed = true;
        }
    } else if (tex_type == 0x0B) {
        // A8
        if (fileData.GetCount() >= 0x0E + (width * height)) {
            size_t data_off = 0x0E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    size_t src = data_off + (y * width + x);
                    uint8_t a = fileData[src];
                    size_t dst = (y * width + x) * 4;
                    rgba[dst]     = 0xFF;
                    rgba[dst + 1] = 0xFF;
                    rgba[dst + 2] = 0xFF;
                    rgba[dst + 3] = a;
                }
            }
            parsed = true;
            // Runtime-paletted: flash/strike/charge FX write their colour ramp
            // into this texture's palette (index = intensity until then).
            paletted8 = true; palIdxOff = 0x0E; palHasClut = false;
        }
    } else if (tex_type == 0x06) {
        // RGBA4444
        if (fileData.GetCount() >= 0x0E + (width * height * 2)) {
            size_t data_off = 0x0E;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    size_t src = data_off + (y * width + x) * 2;
                    uint16_t p = fileData[src] | (fileData[src + 1] << 8);
                    size_t dst = (y * width + x) * 4;
                    rgba[dst]     = (p & 0xF) * 17;
                    rgba[dst + 1] = ((p >> 4) & 0xF) * 17;
                    rgba[dst + 2] = ((p >> 8) & 0xF) * 17;
                    rgba[dst + 3] = ((p >> 12) & 0xF) * 17;
                }
            }
            parsed = true;
        }
    }

    if (!parsed) {
        Errorf("gfxGetTexture: unsupported type 0x%X or truncated file for '%s'", tex_type, name);
        sTextureCache[key] = NULL;
        return NULL;
    }

    // Create D3D11 resources
    ID3D11Device *device = gfxGetDevice();
    if (!device) {
        // Safe mode (e.g. CLI tool without D3D device initialized)
        gfxTexture *tex = new gfxTexture();
        tex->Name = name;
        tex->Width = width;
        tex->Height = height;
        sTextureCache[key] = tex;
        return tex;
    }

    ID3D11Texture2D *d3dTex = NULL;
    ID3D11ShaderResourceView *srv = NULL;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = width * 4;

    HRESULT hr = device->CreateTexture2D(&desc, &initData, &d3dTex);
    if (SUCCEEDED(hr)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(d3dTex, &srvDesc, &srv);
    }

    if (FAILED(hr)) {
        if (d3dTex) d3dTex->Release();
        Errorf("gfxGetTexture: D3D resource creation failed for '%s'", name);
        sTextureCache[key] = NULL;
        return NULL;
    }

    bool hasAlpha = false;
    bool hasOpaque = false;
    for (size_t p = 3; p < rgba.GetCount(); p += 4) {
        if (rgba[p] < 254) {
            hasAlpha = true;
        } else {
            hasOpaque = true;
        }
    }

    gfxTexture *tex = new gfxTexture();
    tex->Name = name;
    tex->Width = width;
    tex->Height = height;
    tex->D3DTexture = d3dTex;
    tex->SRV = srv;
    tex->m_HasAlpha = hasAlpha;
    tex->m_AllTranslucent = (hasAlpha && !hasOpaque);
    if (paletted8) {
        tex->m_Indices.assign(&fileData[palIdxOff], &fileData[palIdxOff] + width * height);
        tex->m_Palette = new gfxTexturePalette();
        tex->m_Palette->Owner = tex;
        for (int i = 0; i < 256; i++) {
            gfxImageRGBA e;
            if (palHasClut) { const uint8_t *c = &fileData[0x0E + i * 4]; e.Set(c[2], c[1], c[0], c[3]); }
            else e.Set(255, 255, 255, i);
            tex->m_Palette->SetEntry(i, e);
        }
    }

    sTextureCache[key] = tex;
    return tex;
}

void gfxTexturePalette::Update() {
    if (Owner) Owner->UpdateFromPalette();
}

void gfxTexture::UpdateFromPalette() {
    if (!m_Palette || m_Indices.empty() || !D3DTexture) return;
    ID3D11DeviceContext *ctx = gfxGetContext();
    if (!ctx) return;
    std::vector<uint8_t> rgba(m_Indices.size() * 4);
    bool hasAlpha = false;
    for (size_t i = 0; i < m_Indices.size(); i++) {
        const gfxImageRGBA &e = m_Palette->GetEntry(m_Indices[i]);
        rgba[i * 4] = e.r; rgba[i * 4 + 1] = e.g; rgba[i * 4 + 2] = e.b; rgba[i * 4 + 3] = e.a;
        if (e.a < 254) hasAlpha = true;
    }
    m_HasAlpha = hasAlpha;
    ctx->UpdateSubresource(D3DTexture, 0, NULL, rgba.data(), Width * 4, 0);
}

gfxTexture::gfxTexture() : m_User(nullptr), Width(0), Height(0), D3DTexture(NULL), SRV(NULL), RTV(NULL), m_TexEnv(gfxTexEnvAlpha), RefCount(1), m_HasAlpha(false), m_AllTranslucent(false), m_Palette(NULL) {}

gfxTexture::~gfxTexture() {
    // Unregister every name (including gfxAssociateTexture aliases) pointing at
    // this texture -- the cache is non-owning, so a dead entry would dangle.
    for (auto it = sTextureCache.begin(); it != sTextureCache.end();) {
        if (it->second == this) {
            it = sTextureCache.erase(it);
        } else {
            ++it;
        }
    }
    delete m_Palette;
    if (RTV) { RTV->Release(); }
    if (SRV) { SRV->Release(); }
    if (D3DTexture) { D3DTexture->Release(); }
}

gfxTexture *gfxTexture::CreateRenderTarget(int w, int h, int flags, int offset) {
    if (flags & rtargetBackFBMem) {
        gfxTexture *bb = gfxGetBackBufferCopy();
        if (bb) { bb->AddRef(); return bb; }
    }
    if ((flags & rtargetTexMem) || (w > 0 && h > 0 && !(flags & (rtargetBackFBMem | rtargetFrontFBMem)))) {
        gfxTexture *tex = gfxCreateTextTarget(w, h);
        if (tex) return tex;
    }
    // Front-FB (the swapchain itself) and unknown targets: a marker texture.
    gfxTexture *tex = new gfxTexture();
    tex->Width = w;
    tex->Height = h;
    return tex;
}

void gfxImage::Release() {
    free(m_RGBA);
    delete this;
}

gfxImage *gfxImage::CreateSubImage(int x, int y, int w, int h) const {
    if (w <= 0 || h <= 0) return NULL;
    gfxImage *sub = new gfxImage();
    sub->Width = w;
    sub->Height = h;
    sub->m_RGBA = malloc((size_t)w * h * 4);
    if (!sub->m_RGBA) { delete sub; return NULL; }
    const uint8_t *src = (const uint8_t *)m_RGBA;
    for (int row = 0; row < h; row++) {
        uint8_t *dst = (uint8_t *)sub->m_RGBA + (size_t)row * w * 4;
        int sy = y + row;
        for (int col = 0; col < w; col++) {
            int sx = x + col;
            if (src && sx >= 0 && sx < Width && sy >= 0 && sy < Height) {
                const uint8_t *p = src + ((size_t)sy * Width + sx) * 4;
                dst[col * 4 + 0] = p[0]; dst[col * 4 + 1] = p[1];
                dst[col * 4 + 2] = p[2]; dst[col * 4 + 3] = p[3];
            } else {
                dst[col * 4 + 0] = dst[col * 4 + 1] = dst[col * 4 + 2] = 0;
                dst[col * 4 + 3] = 255;
            }
        }
    }
    return sub;
}

#include <cstdio>
// Uncompressed 32-bit TGA (image type 2, BGRA, top-left origin flag).  Layout
// per the Truevision TGA 2.0 spec (18-byte header, then pixels).
bool gfxSaveTargaImage(const char *name, const gfxImage *img) {
    if (!name || !img || !img->m_RGBA || img->Width <= 0 || img->Height <= 0) return false;
    char path[512];
    strncpy(path, name, sizeof(path) - 5); path[sizeof(path) - 5] = 0;
    size_t n = strlen(path);
    if (n < 4 || _stricmp(path + n - 4, ".tga") != 0) strcat(path, ".tga");
    FILE *f = fopen(path, "wb");
    if (!f) {
        Warningf("gfxSaveTargaImage: can't create '%s'", path);
        return false;
    }
    uint8_t hdr[18] = {0};
    hdr[2] = 2;                                   // uncompressed true-colour
    hdr[12] = (uint8_t)(img->Width & 0xFF);  hdr[13] = (uint8_t)((img->Width >> 8) & 0xFF);
    hdr[14] = (uint8_t)(img->Height & 0xFF); hdr[15] = (uint8_t)((img->Height >> 8) & 0xFF);
    hdr[16] = 32;                                 // bits per pixel
    hdr[17] = 0x28;                               // 8 alpha bits, top-left origin
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    const uint8_t *src = (const uint8_t *)img->m_RGBA;
    uint8_t *row = (uint8_t *)malloc((size_t)img->Width * 4);
    for (int y = 0; ok && row && y < img->Height; y++) {
        const uint8_t *s = src + (size_t)y * img->Width * 4;
        for (int x = 0; x < img->Width; x++) {   // RGBA -> BGRA
            row[x * 4 + 0] = s[x * 4 + 2]; row[x * 4 + 1] = s[x * 4 + 1];
            row[x * 4 + 2] = s[x * 4 + 0]; row[x * 4 + 3] = s[x * 4 + 3];
        }
        ok = fwrite(row, 1, (size_t)img->Width * 4, f) == (size_t)img->Width * 4;
    }
    free(row);
    fclose(f);
    return ok && row != NULL;
}

// Minimal TGA reader (types 2/10, 24/32bpp; type 3/11 8bpp luminance).
gfxImage *gfxLoadTargaImage(const char *name, bool /*mipmaps*/) {
    char base[256];
    strncpy(base, name, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 4 && !_stricmp(base + n - 4, ".tga")) base[n - 4] = 0;
    Stream *st = ASSET.Exists(base, "tga") ? ASSET.Open(base, "tga") : NULL;
    if (!st) {
        std::string k = std::string("texture/") + base;   // same fallback as gfxGetTexture
        if (ASSET.Exists(k.c_str(), "tga")) st = ASSET.Open(k.c_str(), "tga");
    }
    if (!st) {
        Warningf("gfxLoadTargaImage: can't open '%s.tga'", base);
        return NULL;
    }
    atArray<uint8_t> data;
    char buf[4096]; int r;
    while ((r = st->Read(buf, sizeof(buf))) > 0) for (int i = 0; i < r; i++) data.Append((uint8_t)buf[i]);
    st->Close();
    if (data.GetCount() < 18) return NULL;
    int idLen = data[0], imgType = data[2];
    int w = data[12] | (data[13] << 8), h = data[14] | (data[15] << 8);
    int bpp = data[16]; bool topDown = (data[17] & 0x20) != 0;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return NULL;
    if (!(imgType == 2 || imgType == 3 || imgType == 10 || imgType == 11) || !(bpp == 8 || bpp == 24 || bpp == 32)) return NULL;
    int bypp = bpp / 8; size_t total = (size_t)w * h;
    atArray<uint8_t> px; px.Resize((int)(total * bypp));
    size_t off = 18 + idLen, out = 0;
    if (imgType == 10 || imgType == 11) {
        while (out < total * bypp && off < (size_t)data.GetCount()) {
            uint8_t hdr = data[(int)off++]; int count = (hdr & 0x7F) + 1;
            if (hdr & 0x80) {
                if (off + bypp > (size_t)data.GetCount()) break;
                for (int k = 0; k < count && out < total * bypp; k++) { memcpy(&px[(int)out], &data[(int)off], bypp); out += bypp; }
                off += bypp;
            } else {
                for (int k = 0; k < count && out < total * bypp && off + bypp <= (size_t)data.GetCount(); k++) { memcpy(&px[(int)out], &data[(int)off], bypp); off += bypp; out += bypp; }
            }
        }
    } else {
        size_t need = total * bypp;
        if (off + need > (size_t)data.GetCount()) return NULL;
        memcpy(&px[0], &data[(int)off], need);
    }
    gfxImage *img = new gfxImage();
    img->Width = w; img->Height = h;
    img->m_RGBA = malloc(total * 4);
    uint8_t *dst = (uint8_t *)img->m_RGBA;
    for (int y = 0; y < h; y++) {
        int srcY = topDown ? y : (h - 1 - y);
        for (int x = 0; x < w; x++) {
            const uint8_t *sp = &px[(srcY * w + x) * bypp];
            uint8_t *dp = dst + ((size_t)y * w + x) * 4;
            if (bypp == 1)      { dp[0] = dp[1] = dp[2] = sp[0]; dp[3] = 0xFF; }
            else                { dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = (bypp == 4) ? sp[3] : 0xFF; }
        }
    }
    return img;
}

gfxBitmap *gfxBitmap::Create(gfxImage *img) {
    if (!img || !img->m_RGBA) return NULL;
    gfxBitmap *bmp = new gfxBitmap();
    bmp->Width = img->Width; bmp->Height = img->Height;
    ID3D11Device *device = gfxGetDevice();
    if (device) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = img->Width; desc.Height = img->Height;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = img->m_RGBA; init.SysMemPitch = img->Width * 4;
        ID3D11Texture2D *t = NULL;
        if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &t)) && t) {
            gfxTexture *tex = new gfxTexture();
            tex->Name = "bitmap"; tex->Width = img->Width; tex->Height = img->Height;
            tex->D3DTexture = t;
            device->CreateShaderResourceView(t, NULL, &tex->SRV);
            bmp->Texture = tex;
        }
    }
    return bmp;
}

void gfxFreeBitmap(gfxBitmap *bmp) {
    if (!bmp) return;
    if (bmp->Texture) bmp->Texture->Release();
    delete bmp;
}

void gfxFreeTexture(gfxTexture *tex) {
    if (tex) {
        tex->Release();
    }
}

bool gfxTextureResident(const char *name) {
    if (!name || name[0] == '\0') return false;
    auto it = sTextureCache.find(TrimTextureName(name));
    return it != sTextureCache.end() && it->second != NULL;
}

// The inverse of the type 0x12 branch of the loader above: a 14-byte header
// {u16 width, u16 height, u16 type} and then width*height RGBA bytes, top row
// first.  0x12 is the one type that stores the pixels unmodified, so a texture
// written this way reloads exactly as it was.  Bytes 6..13 are not read back
// for this type; they carry the bit depth and mip count the shipped files put
// there.
bool gfxSaveTextureAsset(gfxTexture *tex, const char *name) {
    if (!tex || !name || !name[0]) return false;

    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!gfxReadTexturePixels(tex, rgba, w, h) || w <= 0 || h <= 0) {
        Warningf("gfxSaveTextureAsset: could not read '%s' back off the GPU", name);
        return false;
    }

    Stream *s = ASSET.Create(name, "tex");
    if (!s) {
        Errorf("gfxSaveTextureAsset: can't create '%s.tex'", name);
        return false;
    }

    unsigned char hdr[0x0E];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = (unsigned char)(w & 0xff);         hdr[1] = (unsigned char)((w >> 8) & 0xff);
    hdr[2] = (unsigned char)(h & 0xff);         hdr[3] = (unsigned char)((h >> 8) & 0xff);
    hdr[4] = 0x12;                              hdr[5] = 0;      // RGBA8888
    hdr[6] = 32;                                hdr[7] = 0;      // bits per pixel
    hdr[8] = 1;                                 hdr[9] = 0;      // mip levels
    s->Write(hdr, sizeof(hdr));
    s->Write(&rgba[0], w * h * 4);
    s->Close();

    Displayf("gfxSaveTextureAsset: wrote '%s.tex' (%dx%d RGBA8888)", name, w, h);
    return true;
}

void gfxAssociateTexture(gfxTexture *tex, const char *name) {
    if (!tex || !name || name[0] == '\0') return;
    // Alias an existing texture under another lookup name (fx code registers
    // its loaded textures so later gfxGetTexture calls find them resident).
    // No ref change: the cache is non-owning and the destructor unregisters.
    sTextureCache[TrimTextureName(name)] = tex;
}

bool gfxUpdateRgbaTexture(gfxTexture *tex, int width, int height, const unsigned char *rgba) {
    if (!tex || !rgba || width <= 0 || height <= 0) return false;
    if (tex->Width != width || tex->Height != height) return false;
    if (!tex->D3DTexture) return false;
    ID3D11DeviceContext *ctx = gfxGetContext();
    if (!ctx) return false;
    ctx->UpdateSubresource(tex->D3DTexture, 0, NULL, rgba, (UINT)width * 4, 0);
    return true;
}

// Put a texture the caller already owns into the name cache, so anything that
// binds textures by name can reach it.  The live city environment map is a
// render target, not a file, and the city's daytime window shader asks for it
// by name like any other texture.  The cache is non-owning, so this takes a
// reference on the caller's behalf and keeps it until the name is replaced.
bool gfxRegisterExistingTexture(const char *name, gfxTexture *tex) {
    if (!name || !name[0]) return false;
    std::string key = TrimTextureName(name);
    auto it = sTextureCache.find(key);
    if (!tex) {                       // unregister (the owner is going away)
        if (it == sTextureCache.end()) return false;
        if (it->second) it->second->Release();
        sTextureCache.erase(it);
        return true;
    }
    if (it != sTextureCache.end() && it->second == tex) return true;
    if (it != sTextureCache.end() && it->second) it->second->Release();
    tex->AddRef();
    sTextureCache[key] = tex;
    return true;
}

gfxTexture *gfxRegisterRgbaTexture(const char *name, int width, int height, const unsigned char *rgba) {
    if (!name || !name[0] || width <= 0 || height <= 0 || !rgba) return NULL;
    std::string key = TrimTextureName(name);
    auto it = sTextureCache.find(key);
    if (it != sTextureCache.end() && it->second) {
        return it->second;
    }

    ID3D11Device *device = gfxGetDevice();
    ID3D11Texture2D *d3dTex = NULL;
    ID3D11ShaderResourceView *srv = NULL;

    if (device) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)width;
        desc.Height = (UINT)height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = rgba;
        initData.SysMemPitch = (UINT)width * 4;

        HRESULT hr = device->CreateTexture2D(&desc, &initData, &d3dTex);
        if (SUCCEEDED(hr)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            hr = device->CreateShaderResourceView(d3dTex, &srvDesc, &srv);
        }
        if (FAILED(hr)) {
            if (d3dTex) d3dTex->Release();
            Errorf("gfxRegisterRgbaTexture: D3D resource creation failed for '%s'", name);
            return NULL;
        }
    }

    bool hasAlpha = false;
    bool hasOpaque = false;
    size_t totalPx = (size_t)width * (size_t)height;
    for (size_t i = 0; i < totalPx; ++i) {
        if (rgba[i * 4 + 3] < 254) {
            hasAlpha = true;
        } else {
            hasOpaque = true;
        }
    }

    gfxTexture *tex = new gfxTexture();
    tex->Name = name;
    tex->Width = width;
    tex->Height = height;
    tex->D3DTexture = d3dTex;
    tex->SRV = srv;
    tex->m_HasAlpha = hasAlpha;
    tex->m_AllTranslucent = (hasAlpha && !hasOpaque);

    sTextureCache[key] = tex;
    return tex;
}

// Folder tried after the bare texture name (gfx/texture.h).
const char *gfxLoadImageFolder = "texture";

void gfxImage::Scale(int w, int h) {
    if (w <= 0 || h <= 0 || !m_RGBA || (w == Width && h == Height)) return;
    unsigned char *dst = (unsigned char *)malloc((size_t)w * h * 4);
    if (!dst) return;
    const unsigned char *src = (const unsigned char *)m_RGBA;
    for (int y = 0; y < h; y++) {
        int sy = (int)((long long)y * Height / h);
        for (int x = 0; x < w; x++) {
            int sx = (int)((long long)x * Width / w);
            const unsigned char *s = src + ((size_t)sy * Width + sx) * 4;
            unsigned char *d = dst + ((size_t)y * w + x) * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }
    free(m_RGBA);
    m_RGBA = dst;
    Width = w;
    Height = h;
}

// The texture name cache is non-owning: live entries vanish with their last
// Release(), so the layer-unload prune only has the negative entries (names
// whose load failed) to drop -- the next layer may switch gfxLoadImageFolder
// or ship the file, and a remembered failure would block that load.  Kill
// drops every cached name so a fresh load never aliases a stale texture.
void gfxTexturePruneHashtable() {
    int dropped = 0;
    for (auto it = sTextureCache.begin(); it != sTextureCache.end();) {
        if (!it->second) { it = sTextureCache.erase(it); dropped++; }
        else ++it;
    }
    if (dropped) Displayf("gfxTexturePruneHashtable: forgot %d failed loads, %d textures resident", dropped, (int)sTextureCache.size());
}
void gfxTextureKillHashtable() { sTextureCache.clear(); }

void gfxTexturePrintHashtable() {
    Displayf("gfxTexture cache: %d entries", (int)sTextureCache.size());
    for (auto it = sTextureCache.begin(); it != sTextureCache.end(); ++it) {
        const gfxTexture *t = it->second;
        if (t) Displayf("  %-40s %4dx%-4d refs=%d%s", it->first.c_str(), t->GetWidth(), t->GetHeight(), t->GetRefCount(), t->HasAlpha() ? " alpha" : "");
        else Displayf("  %-40s (missing)", it->first.c_str());
    }
}

////////////////////////////////////////////////////////////////////////////
// gfxTextureTool: tile the cached textures over the current viewport, a
// page at a time, with name and size under each thumbnail.

void gfxTextureTool::Load(const char *layoutName, const char *path) {
    strncpy(m_Layout, layoutName ? layoutName : "", sizeof(m_Layout) - 1); m_Layout[sizeof(m_Layout) - 1] = 0;
    strncpy(m_Path, path ? path : "", sizeof(m_Path) - 1); m_Path[sizeof(m_Path) - 1] = 0;
    if (m_Path[0]) gfxLoadImageFolder = m_Path;
    m_Page = 0;
}

void gfxTextureTool::Draw() {
    if (!pipeManager::sm_Instance) return;
    const gfxViewportParams &p = PIPE.GetViewport()->GetViewportParams();
    const int cell = 96, label = 20;
    int cols = (int)p.m_Width / cell; if (cols < 1) cols = 1;
    int rows = (int)p.m_Height / (cell + label); if (rows < 1) rows = 1;
    int perPage = cols * rows;
    int total = (int)sTextureCache.size();
    int pages = (total + perPage - 1) / perPage; if (pages < 1) pages = 1;
    if (m_Page >= pages) m_Page = pages - 1;

    RSTATE.SetZTestEnable(false);
    RSTATE.SetZWriteEnable(false);
    RSTATE.SetLighting(false);
    RSTATE.SetAlphaBlendEnable(true);
    RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
    RSTATE.SetAlphaFunc(alphaAlways);
    RSTATE.SetTexture(NULL);
    PIPE.ClearRect((int)p.m_X, (int)p.m_Y, (int)p.m_Width, (int)p.m_Height, mkrgba(24, 24, 32, 255));

    int idx = 0, shown = 0;
    for (auto it = sTextureCache.begin(); it != sTextureCache.end(); ++it, ++idx) {
        if (idx < m_Page * perPage) continue;
        if (shown >= perPage) break;
        gfxTexture *t = it->second;
        int cx = (int)p.m_X + (shown % cols) * cell, cy = (int)p.m_Y + (shown / cols) * (cell + label);
        shown++;
        if (t) {
            RSTATE.SetTexture(t);
            PIPE.Blit2D((float)cx + 2, (float)cy + 2, (float)cx + cell - 2, (float)cy + cell - 2,
                        0.0f, 0.0f, (float)t->GetWidth(), (float)t->GetHeight(), 0xffffffffu);
        }
        char text[96];
        snprintf(text, sizeof(text), "%s %dx%d", it->first.c_str(), t ? t->GetWidth() : 0, t ? t->GetHeight() : 0);
        text[14] = 0;   // the stroke font fits ~11 cells per thumbnail
        gfxDrawFont(cx + 2, cy + cell + 2, text, 0xffc0c0c0u);
    }
    char title[128];
    snprintf(title, sizeof(title), "%s  page %d/%d  (%d textures)", m_Layout[0] ? m_Layout : "textures", m_Page + 1, pages, total);
    gfxDrawFont((int)p.m_X + 4, (int)p.m_Y + (int)p.m_Height - 14, title, 0xffffffffu);
    RSTATE.SetTexture(NULL);
}

// A D3D RGBA8 texture and its shader view built from a width*height*4 buffer.
static bool sCreateRgbaD3D(int width, int height, const unsigned char *rgba,
                           ID3D11Texture2D *&outTex, ID3D11ShaderResourceView *&outSrv)
{
    outTex = NULL;
    outSrv = NULL;
    ID3D11Device *device = gfxGetDevice();
    if (!device) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba;
    initData.SysMemPitch = (UINT)width * 4;

    if (FAILED(device->CreateTexture2D(&desc, &initData, &outTex))) {
        outTex = NULL;
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(outTex, &srvDesc, &outSrv))) {
        outTex->Release();
        outTex = NULL;
        outSrv = NULL;
        return false;
    }
    return true;
}

// Resource page-in constructor.  On the PS2 a texture serialised inside a
// resource (flash movie bitmaps and font sheets) was rebuilt in place from its
// GS layout (alpha gfxTexture(datResource&) @0x4e3d18, 160 bytes):
//   +0x90  gfxTexturePalette*: u16 entry count at +0x02, RGBA8 entries from
//          +0x10 with alpha on the GS scale (0x80 = opaque)
//   +0x94  gfxMipChain*: level-0 pixels through +0x08, format byte +0x28,
//          extra mip levels +0x29, width/height u16 at +0x2c/+0x2e
// Format 5 is PSMT8 and format 6 PSMT4.  The mip chain's +0x30 byte is the
// format the pixels are stored in: 1 when they were written through a 32-bit
// frame buffer ("swizzled", decoded as the city's resident textures are,
// rmcore/rscgeom), or the texture's own format when they are plain rows (4-bit
// texels low nibble first).  Every square flash texture seen is swizzled; the
// 512x128 logo bitmaps in bg_savegame are plain, and unswizzling them scrambled
// the logo into red and white blocks.  The pack is data on PC (data/resource.h), so the
// cursor addresses the object and the texture is built from it here.
gfxTexture::gfxTexture(datResource &rsc) : gfxTexture() {
    Name = "resource";
    const datResourceImage *image = rsc.GetImage();
    const u32 base = rsc.Tell();
    if (!image || !image->IsValidAddress(base, 0xa0)) {
        Warningf("gfxTexture(datResource): no texture object at %08x in '%s'", base, rsc.GetName());
        return;
    }
    const u32 palAddr = image->ReadU32(base + 0x90);
    const u32 mipAddr = image->ReadU32(base + 0x94);
    if (!image->IsValidAddress(palAddr, 0x10) || !image->IsValidAddress(mipAddr, 0x30)) {
        Warningf("gfxTexture(datResource): texture %08x in '%s' has no palette/mip chain (%08x/%08x)",
                 base, rsc.GetName(), palAddr, mipAddr);
        return;
    }

    const int format = image->ReadU8(mipAddr + 0x28);
    const int width = image->ReadU16(mipAddr + 0x2c);
    const int height = image->ReadU16(mipAddr + 0x2e);
    const u32 pixAddr = image->ReadU32(mipAddr + 0x08);
    const int numColors = image->ReadU16(palAddr + 0x02);
    const int storedFormat = image->ReadU8(mipAddr + 0x30);
    const bool is8 = (format == 5);
    const bool is4 = (format == 6);
    const int maxColors = is8 ? 256 : 16;
    const u32 pixBytes = is8 ? (u32)(width * height) : (u32)((width * height + 1) / 2);
    if ((!is8 && !is4) || width <= 0 || height <= 0 || width > 2048 || height > 2048 ||
        numColors <= 0 || numColors > maxColors ||
        !image->IsValidAddress(pixAddr, pixBytes) || !image->IsValidAddress(palAddr + 0x10, (u32)numColors * 4)) {
        Warningf("gfxTexture(datResource): texture %08x in '%s' format %d %dx%d, %d colours - not decoded",
                 base, rsc.GetName(), format, width, height, numColors);
        return;
    }

    // The swizzled layouts can reach past width*height near the edges; pad with
    // zeros rather than read outside the image.
    const u32 available = image->GetBase() + image->GetSize() - pixAddr;
    std::vector<unsigned char> src((size_t)pixBytes * 2 + 64, 0);
    memcpy(&src[0], image->At(pixAddr), (size_t)((available < src.size()) ? available : src.size()));

    std::vector<unsigned char> indices((size_t)width * height);
    if (storedFormat == format) {
        const int rowBytes = is8 ? width : (width + 1) / 2;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                indices[(size_t)y * width + x] = is8 ? src[(size_t)y * rowBytes + x]
                                                     : (unsigned char)((src[(size_t)y * rowBytes + x / 2] >> ((x & 1) * 4)) & 0x0f);
            }
        }
    } else {
        if (storedFormat != 1)
            Warningf("gfxTexture(datResource): texture %08x in '%s' stored as format %d (expected 1 or %d), decoding as swizzled",
                     base, rsc.GetName(), storedFormat, format);
        if (is8)
            rscUnswizzlePsmt8(&src[0], &indices[0], width, height);
        else
            rscUnswizzlePsmt4(&src[0], &indices[0], width, height);
    }
    if (is8 && numColors == 256 && ARGS.Get("swizzletest")) {
        const float r0 = rscPsmt8Roughness(&src[0], image->At(palAddr + 0x10), width, height, false);
        const float r1 = rscPsmt8Roughness(&src[0], image->At(palAddr + 0x10), width, height, true);
        Displayf("[swizzle] resource %08x in '%s' %dx%d: noswap %.2f swap %.2f -> %s", base, rsc.GetName(), width, height, r0, r1,
                 r1 < r0 * 0.9f ? "SWAP" : r0 < r1 * 0.9f ? "NOSWAP" : "unclear");
    }

    const u8 *clut = image->At(palAddr + 0x10);
    std::vector<unsigned char> rgba((size_t)width * height * 4);
    bool hasAlpha = false, hasOpaque = false;
    for (size_t i = 0; i < indices.size(); i++) {
        u32 idx = indices[i];
        if (is8 && ((idx & 0x18) == 0x08 || (idx & 0x18) == 0x10)) idx ^= 0x18;   // CSM1 CLUT layout
        if ((int)idx >= numColors) idx = 0;
        const u8 *c = clut + idx * 4;
        const u32 a = (u32)c[3] * 2;
        rgba[i * 4 + 0] = c[0];
        rgba[i * 4 + 1] = c[1];
        rgba[i * 4 + 2] = c[2];
        rgba[i * 4 + 3] = (unsigned char)(a > 255 ? 255 : a);
        if (rgba[i * 4 + 3] < 254) hasAlpha = true; else hasOpaque = true;
    }

    // -flashlog: what each pack texture decoded to.
    {
        const char *p = nullptr;
        if (ARGS.Get("flashlog", 0, &p)) {
            int aMin = 255, aMax = 0;
            u32 zero = 0;
            double sumR = 0, sumG = 0, sumB = 0;
            const size_t count = indices.size();
            for (size_t i = 0; i < count; i++) {
                const int a = rgba[i * 4 + 3];
                if (a < aMin) aMin = a;
                if (a > aMax) aMax = a;
                if (!a) zero++;
                sumR += rgba[i * 4];
                sumG += rgba[i * 4 + 1];
                sumB += rgba[i * 4 + 2];
            }
            Displayf("gfxTexture(datResource): %08x in '%s' format %d %dx%d, %d colours: alpha %d..%d (%u of %u zero), mean rgb %.0f %.0f %.0f",
                     base, rsc.GetName(), format, width, height, numColors, aMin, aMax, zero, (unsigned)count,
                     sumR / count, sumG / count, sumB / count);
        }
    }

    if (!sCreateRgbaD3D(width, height, &rgba[0], D3DTexture, SRV)) {
        Errorf("gfxTexture(datResource): D3D resource creation failed for texture %08x in '%s'", base, rsc.GetName());
        return;
    }
    Width = width;
    Height = height;
    m_HasAlpha = hasAlpha;
    m_AllTranslucent = (hasAlpha && !hasOpaque);
}

void gfxBitmap::Release() { gfxFreeBitmap(this); }

// Image -> texture through the bitmap path (which owns the upload).
gfxTexture *gfxTexture::Create(gfxImage *image, bool /*mipmaps*/) {
    if (!image) return NULL;
    gfxBitmap *bmp = gfxBitmap::Create(image);
    if (!bmp || !bmp->Texture) { if (bmp) gfxFreeBitmap(bmp); return NULL; }
    gfxTexture *tex = bmp->Texture;
    tex->AddRef();        // survive the bitmap's release below
    gfxFreeBitmap(bmp);
    return tex;
}

// Base-name image load: only the TGA decoder returns pixels on this build;
// .tex atlases are uploaded straight to textures by gfxGetTexture.
gfxImage *gfxLoadTexImage(const char *name, bool mipmaps) {
    return gfxLoadTargaImage(name, mipmaps);
}
