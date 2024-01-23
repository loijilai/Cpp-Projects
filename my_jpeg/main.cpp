#include <iostream>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <map>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "qdbmp.h"

// Define markers
const uint16_t SOI = 0xffd8;
const uint16_t APP0 = 0xffe0;
const uint16_t DQT = 0xffdb;
const uint16_t SOF0 = 0xffc0;
const uint16_t DHT = 0xffc4;
const uint16_t SOS = 0xffda;
const uint16_t EOI = 0xffd9;
const uint16_t COM = 0xfffe;

// Marker mapping
std::map<uint16_t, std::string> marker_mapping = {
    {SOI, "Start of Image"},
    {APP0, "APP0"},
    {DQT, "Define Quantization Table"},
    {SOF0, "Start of Frame: Baseline"},
    {DHT, "Define Huffman Table"},
    {SOS, "Start of Scan"},
    {EOI, "End of Image"},
    {COM, "COM"}
};

typedef struct Component {
    // uint8_t id; dirty: use 0:Y, 1:Cb, 2:Cr
    uint8_t hor_sr;
    uint8_t ver_sr;
    uint8_t quan_table_id;
    uint8_t hf_table_ac_id;
    uint8_t hf_table_dc_id;
}Component;

struct RGBColor {
    double R, G, B;
};

struct YCbCrColor {
    double Y, Cb, Cr;
};

class JPEG {
public:
    JPEG(const std::string& filename) {
        // opens file and read all bytes into data vector
        std::ifstream file(filename, std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            data.resize(file.tellg());
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(&data[0]), data.size());
            file.close();
        }
        offset_ = 0;
        max_hor_sr_ = 0;
        max_ver_sr_ = 0;
        mcu_[3][2][2][8][8] = {0};
    }

    void decode() {
        while (offset_ < data.size()) {
            uint16_t marker = (data[offset_] << 8) | data[offset_ + 1];
            std::cout << "**********************************" << std::endl;
            std::cout << marker_mapping[marker] << std::endl;
            offset_ += 2; // skip marker

            if (marker == SOI) {
                continue;
            } 
            else if (marker == DQT) {
                decodeDQT();
            } 
            else if (marker == SOF0) {
                decodeSOF();
            } 
            else if (marker == DHT) {
                decodeDHT();
            } 
            else if (marker == SOS) {
                decodeSOS();
                readData();
                // testData();
                offset_ = data.size() - 2;
            }
            else if (marker == EOI) {
                return;
            }
            else {
                uint16_t length = (data[offset_] << 8) | data[offset_ + 1];
                offset_ += length; // skip segment length
            }

            if (offset_ >= data.size()) {
                std::cout << "offset exceed" << std::endl;
                break;
            }
        }
    }

private:
    size_t offset_;
    std::vector<uint8_t> data;
    // SOF
    uint16_t image_height_;
    uint16_t image_width_;
    uint8_t num_of_components_;
    uint8_t max_hor_sr_;
    uint8_t max_ver_sr_;
    std::vector<Component> components;

    // DHT
    // level, value, symbol
    // TODO: better data structure, smaller data field
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> huffTable_[2][2];
    uint8_t quantTable_[2][8][8];
    double mcu_[3][2][2][8][8]; // idct requires double data type for accuracy

    // -------------------------------------------------------------
    // Store quantTable_;
    void decodeDQT(void) {
        uint16_t length = (data[offset_] << 8) | data[offset_ + 1];
        std::cout << "Section length: " << length << std::endl;
        length -= 2;
        offset_ += 2;
        while(length) {
            uint8_t table_info = data[offset_++];
            uint8_t table_id = table_info & 0x0f;
            uint8_t precision = table_info >> 4;
            std::cout << "--------------" << std::endl;
            std::cout << "Table info: " << static_cast<int>(table_id) << std::endl;

            // read quantization table
            if(precision == 0) { // 1 byte
                for(int i = 0; i < 8; i++) {
                    for(int j = 0; j < 8; j++) {
                        this->quantTable_[table_id][i][j] = data[offset_++];
                        std::cout << std::setw(3) << static_cast<int>(quantTable_[table_id][i][j]) << " ";
                    }
                    std::cout << std::endl;
                }
                length -= 65; // 64+table_info
            }
            else { // 2 bytes
                // TODO
            }
        }
    }

    // -------------------------------------------------------------
    // Store image_height_, image_width_, num_of_components_, max_hor_sr_, max_ver_sr
    // components: {hor_sr, ver_sr, quan_table_id}
    void decodeSOF(void) {
        uint16_t length = (data[offset_] << 8) | data[offset_ + 1];
        std::cout << "Section length: " << length << std::endl;
        offset_ += 2;
        uint8_t precision = data[offset_++];
        if(precision != 8)
            std::cout << "Precision may not be supported by most software" << std::endl;
        this->image_height_ = (data[offset_] << 8) | data[offset_ + 1];
        offset_ += 2;
        this->image_width_ = (data[offset_] << 8) | data[offset_ + 1];
        offset_ += 2;
        this->num_of_components_ = data[offset_++];
        std::cout << "Precision: " << static_cast<int>(precision) << std::endl;
        std::cout << "Image height: " << image_height_ << std::endl;
        std::cout << "Image width: " << image_width_ << std::endl;
        std::cout << "Num of components: " << static_cast<int>(num_of_components_) << std::endl;

        uint8_t component_id, sampling_factor, quan_table_id;
        uint8_t ver_sr, hor_sr; // horizontal and vertical sampling rate
        Component c;
        for(int i = 0; i < num_of_components_; i++){
            component_id = data[offset_++];
            sampling_factor = data[offset_++];
            ver_sr = sampling_factor & 0x0f;
            hor_sr = sampling_factor >> 4;
            quan_table_id = data[offset_++];
            this->max_hor_sr_ = std::max(hor_sr, this->max_hor_sr_);
            this->max_ver_sr_ = std::max(ver_sr, this->max_ver_sr_);
            // c.id = component_id;
            c.hor_sr = hor_sr;
            c.ver_sr = ver_sr;
            c.quan_table_id = quan_table_id;
            this->components.push_back(c);
            std::cout << "Component: " << static_cast<int>(component_id) 
                      << " Sampling factor(hor*ver): " << static_cast<int>(hor_sr) << " * " 
                      << static_cast<int>(ver_sr)
                      << " Qantization Table ID: " << static_cast<int>(quan_table_id) << std::endl;
        }
    }

    // -------------------------------------------------------------
    // Store huffTable_
    void decodeDHT(void) {
        uint16_t length = (data[offset_] << 8) | data[offset_ + 1];
        std::cout << "Section length: " << length << std::endl;
        length -= 2;
        offset_ += 2;
        while(length) {
            uint8_t table_info = data[offset_++];
            uint8_t table_id = table_info & 0x0f; // get lower four bits
            bool ac_table = table_info >> 4; // get higher four bits
            std::cout << "--------------" << std::endl;
            std::cout << "Table info: " << (ac_table?"AC":"DC") << static_cast<int>(table_id) << std::endl;

            // Reading Huffman table (16 bytes)
            int number = 0; // number of symbols
            std::vector<uint8_t> huffman_table(16);
            for(int i = 0; i < 16; i++) {
                huffman_table[i] = data[offset_++];
                number += huffman_table[i];
            }

            // <level, codeword>
            std::vector<std::pair<uint8_t, uint32_t>> vector_of_pair = createHuffCode(huffman_table); 

            // Reading source symbols based on count in Huffman table
            std::vector<uint8_t> symbols;
            for(int i = 0; i < number; i++) {
                symbols.push_back(data[offset_ + i]);
                this->huffTable_[ac_table][table_id][vector_of_pair[i]] = data[offset_ + i];
            }
            offset_ += number;
            // outputs:
            std::cout << "Symbol table: ";
            for(uint8_t symbol : symbols) {
                std::cout << (int)symbol << " ";
            }
            std::cout << std::endl;
            length -= (1 + 16 + number);
        }
    }

    // -------------------------------------------------------------
    // Store components: {hf_table_ac_id, hf_table_dc_id}
    void decodeSOS(void) {
        uint16_t length = (data[offset_] << 8) | data[offset_ + 1];
        std::cout << "Section length: " << length << std::endl; 
        offset_ += 2;
        offset_++; // skip num_of_components
        uint8_t component_id, hf_table_id, hf_table_dc, hf_table_ac;
        for(int i = 0; i < num_of_components_; i++) {
            component_id = data[offset_++];
            hf_table_id = data[offset_++];
            hf_table_ac = hf_table_id & 0x0f;
            hf_table_dc = hf_table_id >> 4;
            this->components[component_id-1].hf_table_ac_id = hf_table_ac;
            this->components[component_id-1].hf_table_dc_id = hf_table_dc;
            std::cout << "Component: " << static_cast<int>(component_id) 
                      << " Huffman Table ID: " 
                      << "DC - " << static_cast<int>(hf_table_dc) 
                      << " AC - " << static_cast<int>(hf_table_ac)
                      << std::endl;
        }
        offset_ += 3; // skip 0x003F00 will not be used in baseline
    }

    void readData(void) {
        std::vector<uint8_t> comp_data;
        removeFF00(data, comp_data);
        int mcu_height = 8*max_ver_sr_;
        int mcu_width = 8*max_hor_sr_;
        int mcu_ver_num = ceil(image_height_ / static_cast<double>(mcu_height));
        int mcu_hor_num = ceil(image_width_ / static_cast<double>(mcu_width));

        BMP *bmp = BMP_Create(mcu_width * mcu_hor_num, mcu_height * mcu_ver_num, 24);
        for(int i = 0; i < mcu_ver_num; i++) {
            for(int j = 0; j < mcu_hor_num; j++) {
                readMCU(comp_data); // update mcu_
                deQuantize();
                deZigzag();
                idct();
                auto upsample_mcu_ycbcr = upsampling(mcu_height, mcu_width);
                auto mcu_rgb = toRGB(upsample_mcu_ycbcr, mcu_height, mcu_width);
                for (int y = i*mcu_height; y < (i+1)*mcu_height; y++) {
                    for (int x = j*mcu_width; x < (j+1)*mcu_width; x++) {
                        int by = y - i*mcu_height;
                        int bx = x - j*mcu_width;
                        BMP_SetPixelRGB(bmp, x, y, mcu_rgb[by][bx].R, mcu_rgb[by][bx].G, mcu_rgb[by][bx].B);
                    }
                }
            }
        }
        BMP_WriteFile(bmp, "out.bmp");
    }

    void readMCU(const std::vector<uint8_t>& comp_data) {
        for(int comp = 0; comp < num_of_components_; comp++)  {
            for(int j = 0; j < components[comp].ver_sr; j++) {
                for(int k = 0; k < components[comp].hor_sr; k++) {
                    // Read block
                    // std::cout << "comp: " << comp << std::endl;
                    readDC(comp_data, comp, j, k);
                    readAC(comp_data, comp, j, k);
                }
            }
        }
    }

    void readDC(const std::vector<uint8_t>& comp_data, uint8_t comp, int j, int k) {
        static int dc[3] = {0, 0, 0};
        uint8_t length = matchHuff(comp_data, 0, components[comp].hf_table_dc_id);
        if(length == 0) {
            dc[comp] += 0;
            this->mcu_[comp][j][k][0][0] = dc[comp];
            // std::cout << std::setw(4) << dc[comp] << " ";
            return;
        }
        bool sign = getBit(comp_data);
        int dcValue = sign;
        for(int i = 1; i < length; i++) {
            dcValue <<= 1;
            dcValue |= getBit(comp_data);
        }
        dcValue = sign ? dcValue:(dcValue-((1<<length)-1));
        dc[comp] += dcValue;
        this->mcu_[comp][j][k][0][0] = dc[comp];
        // std::cout << std::setw(4) << dc[comp] << " ";
    }

    void readAC(const std::vector<uint8_t>& comp_data, uint8_t comp, int j, int k) {
        int count = 1;
        while (count < 64) {
            uint8_t acinfo = matchHuff(comp_data, 1, components[comp].hf_table_ac_id);
            uint8_t zeros = acinfo >> 4;
            uint8_t length = acinfo & 0x0F;

            // all zeros
            if (zeros == 0 && length == 0) {
                while (count < 64) {
                    this->mcu_[comp][j][k][count/8][count%8] = 0;
                    count++;
                }
            } 
            // 16 subsequent zeros
            else if (zeros == 0x0F && length == 0) { 
                for(int i = 0; i < 16; i++) {
                    this->mcu_[comp][j][k][count/8][count%8] = 0;
                    count++;
                }
            }
            else {
                bool sign = getBit(comp_data);
                int acValue = sign;
                for(int i = 1; i < length; i++) {
                    acValue <<= 1;
                    acValue |= getBit(comp_data);
                }
                acValue = sign ? acValue:(acValue-((1<<length)-1));

                for (int i = 0; i < zeros; i++) {
                    this->mcu_[comp][j][k][count/8][count%8] = 0;
                    count++;
                }
                this->mcu_[comp][j][k][count/8][count%8] = acValue;
                count++;
            }
        }
    }

    void deQuantize(void) {
        for(int comp = 0; comp < num_of_components_; comp++) {
            for(int h = 0; h < components[comp].ver_sr; h++) {
                for(int w = 0; w < components[comp].hor_sr; w++) {
                    for(int i = 0; i < 8; i++) {
                        for(int j = 0; j < 8; j++) {
                            mcu_[comp][h][w][i][j] *= quantTable_[components[comp].quan_table_id][i][j];
                        }
                    }
                }
            }
        }
    }

    void deZigzag(void) {
        for(int comp = 0; comp < num_of_components_; comp++) {
            for(int h = 0; h < components[comp].ver_sr; h++) {
                for(int w = 0; w < components[comp].hor_sr; w++) {
                    int zz[8][8] = {
                            { 0,  1,  5,  6, 14, 15, 27, 28},
                            { 2,  4,  7, 13, 16, 26, 29, 42},
                            { 3,  8, 12, 17, 25, 30, 41, 43},
                            { 9, 11, 18, 24, 31, 40, 44, 53},
                            {10, 19, 23, 32, 39, 45, 52, 54},
                            {20, 22, 33, 38, 46, 51, 55, 60},
                            {21, 34, 37, 47, 50, 56, 59, 61},
                            {35, 36, 48, 49, 57, 58, 62, 63}
                    };
                    for (int i = 0; i < 8; i++) {
                        for (int j = 0; j < 8; j++) {
                            zz[i][j] = mcu_[comp][h][w][zz[i][j] / 8][zz[i][j] % 8];
                        }
                    }
                    for (int i = 0; i < 8; i++) {
                        for (int j = 0; j < 8; j++) {
                            mcu_[comp][h][w][i][j] = zz[i][j];
                        }
                    }
                }
            }
        } 
    }

    void idct(void) {
        const double PI = 3.14159265358979323846;
        double temp[8][8];

        for(int comp = 0; comp < num_of_components_; comp++) {
            for(int h = 0; h < components[comp].ver_sr; h++) {
                for(int w = 0; w < components[comp].hor_sr; w++) {
                    for (int x = 0; x < 8; x++) {
                        for (int y = 0; y < 8; y++) {
                            temp[x][y] = 0;
                            for (int u = 0; u < 8; u++) {
                                for (int v = 0; v < 8; v++) {
                                    double Cu = (u == 0) ? 1 / sqrt(2) : 1;
                                    double Cv = (v == 0) ? 1 / sqrt(2) : 1;
                                    temp[x][y] += Cu * Cv * mcu_[comp][h][w][u][v] *
                                                  cos((2 * x + 1) * u * PI / 16.0) *
                                                  cos((2 * y + 1) * v * PI / 16.0);
                                }
                            }
                            temp[x][y] /= 4.0; // Normalization factor
                        }
                    }
                    // Copying the temporary results back to mcu_
                    for (int i = 0; i < 8; i++) {
                        for (int j = 0; j < 8; j++) {
                            mcu_[comp][h][w][i][j] = temp[i][j];
                        }
                    }
                }
            }
        }
    }

    std::vector<std::vector<YCbCrColor>> upsampling(int mcu_height, int mcu_width) {
        std::vector<std::vector<YCbCrColor>> upsample_mcu_ycbcr(mcu_height, std::vector<YCbCrColor>(mcu_width));
        for (int i = 0; i < mcu_height; i++) {
            for (int j = 0; j < mcu_width; j++) {
                // handle color components one at a time
                for(int comp = 0; comp < num_of_components_; comp++) {
                    // find the index responsible for color at MCU(i, j)
                    int up_i = i * components[comp].ver_sr / max_ver_sr_;
                    int up_j = j * components[comp].hor_sr / max_hor_sr_;
                    int block_i = up_i / 8; 
                    int block_j = up_j / 8;
                    int pixel_i = up_i % 8;
                    int pixel_j = up_j % 8;
                    switch (comp)
                    {
                    case 0:
                        upsample_mcu_ycbcr[i][j].Y = mcu_[comp][block_i][block_j][pixel_i][pixel_j];
                        break;
                    case 1:
                        upsample_mcu_ycbcr[i][j].Cb = mcu_[comp][block_i][block_j][pixel_i][pixel_j];
                        break;
                    case 2:
                        upsample_mcu_ycbcr[i][j].Cr = mcu_[comp][block_i][block_j][pixel_i][pixel_j];
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        return upsample_mcu_ycbcr;
    }

    std::vector<std::vector<RGBColor>> 
        toRGB(std::vector<std::vector<YCbCrColor>> upsample_mcu_ycbcr, int mcu_height, int mcu_width) {

        std::vector<std::vector<RGBColor>> mcu_rgb(mcu_height, std::vector<RGBColor>(mcu_width));

        for (int i = 0; i < mcu_height; ++i) {
            for (int j = 0; j < mcu_width; ++j) {
                double Y = upsample_mcu_ycbcr[i][j].Y;
                double Cb = upsample_mcu_ycbcr[i][j].Cb;
                double Cr = upsample_mcu_ycbcr[i][j].Cr;

                RGBColor pixel = mcu_rgb[i][j];
                pixel.R = std::min(std::max(Y + 1.402*Cr + 128, 0.0), 255.0);
                pixel.G = std::min(std::max((Y - 0.344136*Cb - 0.714136*Cr + 128), 0.0), 255.0);
                pixel.B = std::min(std::max((Y + 1.772*Cb + 128), 0.0), 255.0);
                mcu_rgb[i][j] = pixel;
            }
        }
        return mcu_rgb;
    }

    // -------------------------------------------------------------
    void removeFF00(const std::vector<uint8_t>& data, std::vector<uint8_t>& comp_data) {
        uint8_t b1, b2;
        size_t i = 0;
        while(true) {
            b1 = data[offset_+i];
            b2 = data[offset_+i+1];
            if(b1 == 0xff) {
                if(b2 != 0) // encounter real marker
                    break;
                comp_data.push_back(b1);
                i += 2;
            }
            else {
                comp_data.push_back(b1);
                i += 1;
            }
        }
    }

    bool getBit(const std::vector<uint8_t>& comp_data) {
        static uint8_t buf;
        static size_t byteIndex = 0;
        static uint8_t bitIndex = 0;
        if (bitIndex == 0) 
            buf = comp_data[byteIndex++];
        bool bit = buf & (1 << (7 - bitIndex));
        bitIndex = (bitIndex == 7 ? 0 : bitIndex + 1);
        return bit;
    }

    uint8_t matchHuff(const std::vector<uint8_t>& comp_data, uint8_t is_ac, uint8_t tableID) {
        uint32_t code = 0;
        uint8_t codeLen;
        for (int level = 1; level <= 16; level++) {
            code = code << 1;
            code += getBit(comp_data);
            if (huffTable_[is_ac][tableID].find(std::make_pair(level, code)) != huffTable_[is_ac][tableID].end()) {
                codeLen = huffTable_[is_ac][tableID][std::make_pair(level, code)];
                return codeLen;
            }
        }
    }

    std::vector<std::pair<uint8_t, uint32_t>> createHuffCode(const std::vector<uint8_t>& huffman_table) {
        std::vector<std::pair<uint8_t, uint32_t>> vector_of_pair;
        int code = 0;
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < huffman_table[i]; j++) {
                vector_of_pair.push_back(std::make_pair(i + 1, code));
                code += 1;
            }
            code = code << 1;
        }
        return vector_of_pair;
    }

};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: ./main <jpeg file>\n");
        return 1;
    }
    JPEG jpeg(argv[1]);
    jpeg.decode();
    std::cout << "bmp file generated!" << std::endl;
    return 0;
}
