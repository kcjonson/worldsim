#include "worldgen/debug/DebugImageExporter.h"

#include "worldgen/debug/ColorMaps.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace worldgen {

namespace {

// Write a 24-bit BMP file to path from an RGB pixel buffer (rows bottom-to-top in BMP).
// pixels is width*height*3 bytes, row-major top-to-bottom (we flip on write).
bool writeBmp(const std::string& path, int width, int height, const uint8_t* pixels) {
    // BMP row size must be padded to 4 bytes
    int rowBytes  = width * 3;
    int rowPadded = (rowBytes + 3) & ~3;
    int dataSize  = rowPadded * height;
    int fileSize  = 54 + dataSize;

    uint8_t header[54]{};
    // BMP file header (14 bytes)
    header[0] = 'B'; header[1] = 'M';
    auto writeLE32 = [&](int offset, uint32_t val) {
        header[offset+0] = static_cast<uint8_t>(val);
        header[offset+1] = static_cast<uint8_t>(val >> 8);
        header[offset+2] = static_cast<uint8_t>(val >> 16);
        header[offset+3] = static_cast<uint8_t>(val >> 24);
    };
    auto writeLE16 = [&](int offset, uint16_t val) {
        header[offset+0] = static_cast<uint8_t>(val);
        header[offset+1] = static_cast<uint8_t>(val >> 8);
    };
    writeLE32(2, static_cast<uint32_t>(fileSize));
    writeLE32(10, 54u); // pixel data offset
    // DIB header (40 bytes at offset 14)
    writeLE32(14, 40u);                              // header size
    writeLE32(18, static_cast<uint32_t>(width));
    writeLE32(22, static_cast<uint32_t>(height));
    writeLE16(26, 1u);  // color planes
    writeLE16(28, 24u); // bits per pixel
    // compression = 0 (none), rest 0

    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;

    std::fwrite(header, 1, 54, fp);

    // BMP stores rows bottom-to-top; our pixels are top-to-bottom.
    std::vector<uint8_t> rowBuf(static_cast<size_t>(rowPadded), 0);
    for (int y = height - 1; y >= 0; --y) {
        const uint8_t* src = pixels + static_cast<ptrdiff_t>(y * width * 3);
        // BMP uses BGR byte order
        for (int x = 0; x < width; ++x) {
            rowBuf[static_cast<size_t>(x * 3 + 0)] = src[x * 3 + 2]; // B
            rowBuf[static_cast<size_t>(x * 3 + 1)] = src[x * 3 + 1]; // G
            rowBuf[static_cast<size_t>(x * 3 + 2)] = src[x * 3 + 0]; // R
        }
        std::fwrite(rowBuf.data(), 1, static_cast<size_t>(rowPadded), fp);
    }

    std::fclose(fp);
    return true;
}

} // namespace

bool exportEquirectangularBmp(const GeneratedWorld& world,
                              WorldFieldOrMode mode,
                              const std::string& path,
                              int width) {
    if (!world.grid) return false;
    int height = width / 2;

    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 3), 0);

    for (int py = 0; py < height; ++py) {
        double lat = 90.0 - (py + 0.5) * 180.0 / height;
        for (int px = 0; px < width; ++px) {
            double lon = -180.0 + (px + 0.5) * 360.0 / width;

            TileId t = world.grid->fromLatLon(lat, lon);
            if (t == kInvalidTile) continue;

            Rgb color{};
            switch (mode) {
                case WorldFieldOrMode::Elevation: {
                    float elev = world.data.elevation[t];
                    color = elevationColor(elev, world.seaLevelMeters);
                    break;
                }
                case WorldFieldOrMode::Temperature: {
                    float tempC = static_cast<float>(world.data.temperatureMean[t]) * 0.1f;
                    color = temperatureColor(tempC);
                    break;
                }
                case WorldFieldOrMode::Precipitation: {
                    float precip = static_cast<float>(world.data.precipitation[t]);
                    color = precipitationColor(precip);
                    break;
                }
                case WorldFieldOrMode::Biome: {
                    auto idx = static_cast<size_t>(world.data.biome[t]);
                    if (idx < kBiomeColors.size()) color = kBiomeColors[idx];
                    break;
                }
                case WorldFieldOrMode::PlateId: {
                    color = plateColor(world.data.plateId[t]);
                    break;
                }
                case WorldFieldOrMode::Ocean: {
                    if ((world.data.flags[t] & kFlagOcean) != 0) {
                        color = {0, 80, 160};
                    } else {
                        color = {100, 160, 80};
                    }
                    break;
                }
                case WorldFieldOrMode::Crust: {
                    bool isCrust = (world.data.flags[t] & kFlagContinentalCrust) != 0;
                    color = isCrust ? Rgb{20, 100, 30} : Rgb{0, 30, 90};

                    // Plate boundary: 1px black where any neighbor has a different plateId
                    uint8_t pid = world.data.plateId[t];
                    std::array<TileId, 6> bndNbrs{};
                    uint32_t bndCount = world.grid->neighbors(t, bndNbrs);
                    for (uint32_t bn = 0; bn < bndCount; ++bn) {
                        if (world.data.plateId[bndNbrs[bn]] != pid) {
                            color = {0, 0, 0};
                            break;
                        }
                    }
                    break;
                }
                case WorldFieldOrMode::BoundaryTypeMap: {
                    // BoundaryType color key:
                    //   0 None          → interior tile; tinted by crust type
                    //   1 ConvergentCC  → red    (220, 30, 30)
                    //   2 ConvergentCO  → orange (220, 140, 30)
                    //   3 ConvergentOO  → yellow (220, 210, 30)
                    //   4 Divergent     → blue   (30, 80, 220)
                    //   5 Transform     → green  (30, 180, 80)
                    uint8_t bt = world.data.boundaryType[t];
                    switch (bt) {
                        case 1: color = {220, 30,  30};  break; // ConvergentCC
                        case 2: color = {220, 140, 30};  break; // ConvergentCO
                        case 3: color = {220, 210, 30};  break; // ConvergentOO
                        case 4: color = {30,  80,  220}; break; // Divergent
                        case 5: color = {30,  180, 80};  break; // Transform
                        default: {
                            bool isCrust = (world.data.flags[t] & kFlagContinentalCrust) != 0;
                            color = isCrust ? Rgb{100, 90, 70} : Rgb{20, 30, 60};
                            break;
                        }
                    }
                    break;
                }
                case WorldFieldOrMode::CrustAge: {
                    bool isCont = (world.data.flags[t] & kFlagContinentalCrust) != 0;
                    ExportRgb c = crustAgeColor(world.data.crustAge[t], isCont);
                    color = {c.r, c.g, c.b};
                    break;
                }
                case WorldFieldOrMode::OrogenyAge: {
                    bool isCont = (world.data.flags[t] & kFlagContinentalCrust) != 0;
                    int32_t age = (world.data.orogenyAge[t] == 65535u)
                                  ? tectonics::kOrogenyNever
                                  : static_cast<int32_t>(world.data.orogenyAge[t]);
                    ExportRgb c = orogenyAgeColor(age, isCont);
                    color = {c.r, c.g, c.b};
                    break;
                }
            }

            size_t idx = static_cast<size_t>((py * width + px) * 3);
            pixels[idx + 0] = color.r;
            pixels[idx + 1] = color.g;
            pixels[idx + 2] = color.b;
        }
    }

    return writeBmp(path, width, height, pixels.data());
}

bool exportEquirectangularBmp(const SphereGrid& grid,
                              const std::function<ExportRgb(TileId)>& colorOf,
                              const std::string& path,
                              int width) {
    int height = width / 2;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 3), 0);
    for (int py = 0; py < height; ++py) {
        double lat = 90.0 - (py + 0.5) * 180.0 / height;
        for (int px = 0; px < width; ++px) {
            double lon = -180.0 + (px + 0.5) * 360.0 / width;
            TileId t = grid.fromLatLon(lat, lon);
            if (t == kInvalidTile) continue;
            ExportRgb c = colorOf(t);
            size_t idx = static_cast<size_t>((py * width + px) * 3);
            pixels[idx + 0] = c.r;
            pixels[idx + 1] = c.g;
            pixels[idx + 2] = c.b;
        }
    }
    return writeBmp(path, width, height, pixels.data());
}

} // namespace worldgen
