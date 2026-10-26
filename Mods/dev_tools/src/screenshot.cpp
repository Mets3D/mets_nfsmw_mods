/*
 * The back buffer is copied (StretchRect: this also resolves a multisampled one) into a render target of its own size,
 * read back into system memory, shrunk with a box filter and written as a PNG with stored (uncompressed) deflate blocks, so
 * no compression library is needed. The file is for the eyes of whoever reads it, and a shrunk picture costs a fraction of
 * the tokens of the full one.
 */
#include "screenshot.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "dev_tools.h"

namespace dev_tools::screenshot {

namespace {

template <typename T>
struct Release {
    T *p = nullptr;
    ~Release() {
        if (p) p->Release();
    }
};

uint32_t crc32(const unsigned char *data, size_t size, uint32_t crc = 0) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<unsigned char> &out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<unsigned char>(v >> shift));
}

void chunk(std::vector<unsigned char> &out, const char *type, const std::vector<unsigned char> &data) {
    put32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}

// rgb: width * height * 3 bytes
std::vector<unsigned char> encode_png(const std::vector<unsigned char> &rgb, int width, int height) {
    std::vector<unsigned char> raw;  // each row preceded by filter type 0
    raw.reserve(rgb.size() + height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * width * 3, rgb.begin() + static_cast<size_t>(y + 1) * width * 3);
    }
    std::vector<unsigned char> zlib = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (const unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(n & 0xFF);
        zlib.push_back(n >> 8);
        zlib.push_back(~n & 0xFF);
        zlib.push_back((~n >> 8) & 0xFF);
        zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (last) break;
    }
    put32(zlib, (b << 16) | a);

    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<unsigned char> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bit RGB
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", zlib);
    chunk(png, "IEND", {});
    return png;
}

// One pixel of the locked surface as 0..255 r, g, b.
void read_pixel(const unsigned char *row, int x, D3DFORMAT format, unsigned char *rgb) {
    if (format == D3DFMT_R5G6B5) {
        const uint16_t v = reinterpret_cast<const uint16_t *>(row)[x];
        rgb[0] = ((v >> 11) & 31) * 255 / 31;
        rgb[1] = ((v >> 5) & 63) * 255 / 63;
        rgb[2] = (v & 31) * 255 / 31;
    } else {  // X8R8G8B8, A8R8G8B8: bytes b, g, r, x
        rgb[0] = row[x * 4 + 2];
        rgb[1] = row[x * 4 + 1];
        rgb[2] = row[x * 4];
    }
}

}  // namespace

bool save(IDirect3DDevice9 *device, const char *path, int max_width) {
    Release<IDirect3DSurface9> back, target, system;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p))) return false;
    D3DSURFACE_DESC desc;
    back.p->GetDesc(&desc);
    if (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_R5G6B5) {
        g_log.line("screenshot: back buffer format %d is not handled", desc.Format);
        return false;
    }
    const HRESULT steps[] = {
        device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &target.p, nullptr),
        device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &system.p, nullptr),
    };
    HRESULT copy = steps[0] ? steps[0] : steps[1];
    if (!copy) copy = device->StretchRect(back.p, nullptr, target.p, nullptr, D3DTEXF_NONE);
    if (!copy) copy = device->GetRenderTargetData(target.p, system.p);
    if (FAILED(copy)) {
        g_log.line("screenshot: could not copy the back buffer (%#lx)", static_cast<unsigned long>(copy));
        return false;
    }
    D3DLOCKED_RECT locked;
    if (FAILED(system.p->LockRect(&locked, nullptr, D3DLOCK_READONLY))) return false;

    const int src_w = desc.Width, src_h = desc.Height;
    const int out_w = max_width > 0 && max_width < src_w ? max_width : src_w;
    const int out_h = static_cast<int>(static_cast<int64_t>(src_h) * out_w / src_w);
    std::vector<unsigned char> rgb(static_cast<size_t>(out_w) * out_h * 3);
    const auto *bits = static_cast<const unsigned char *>(locked.pBits);
    for (int y = 0; y < out_h; ++y) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * src_h / out_h), y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * src_h / out_h));
        for (int x = 0; x < out_w; ++x) {
            const int x0 = static_cast<int>(static_cast<int64_t>(x) * src_w / out_w), x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * src_w / out_w));
            unsigned sum[3] = {0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    unsigned char px[3];
                    read_pixel(bits + static_cast<size_t>(sy) * locked.Pitch, sx, desc.Format, px);
                    for (int c = 0; c < 3; ++c) sum[c] += px[c];
                }
            const unsigned n = (y1 - y0) * (x1 - x0);
            unsigned char *dst = &rgb[(static_cast<size_t>(y) * out_w + x) * 3];
            for (int c = 0; c < 3; ++c) dst[c] = static_cast<unsigned char>(sum[c] / n);
        }
    }
    system.p->UnlockRect();

    const std::vector<unsigned char> png = encode_png(rgb, out_w, out_h);
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    const bool ok = fwrite(png.data(), 1, png.size(), file) == png.size();
    fclose(file);
    g_log.line("screenshot: %s (%dx%d from %ux%u)", path, out_w, out_h, desc.Width, desc.Height);
    return ok;
}

}  // namespace dev_tools::screenshot
