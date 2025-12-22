#pragma once

#include <cstdint>
#include <istream>
#include <ostream>

#include "hamming_options.h"

namespace hamarc {

class HammingCodec {
 public:
  explicit HammingCodec(const HammingOptions& opts);

  bool EncodeStream(std::istream& in, std::ostream& out);

  bool DecodeStream(std::istream& in, std::ostream& out,
                    std::uint64_t original_size, std::uint64_t encoded_size);

  int DataBits() const { return data_bits_; }
  int ParityBits() const { return parity_bits_; }

 private:
  int data_bits_;
  int parity_bits_;
  int total_bits_;

  bool EncodeAndWriteBlock(std::uint32_t data_block, unsigned char& out_byte,
                           int& out_bit_count, std::ostream& out);

  bool FlushOutputByte(unsigned char out_byte, int out_bit_count, std::ostream& out);

  bool ReadNextEncodedBit(std::istream& in, unsigned char& in_byte,
                          int& in_bits_available, int& bit_value);

  bool WriteDecodedBitToStream(int bit_value, unsigned char& out_byte,
                               int& out_bit_fill, std::ostream& out);

  std::uint32_t EncodeBlock(std::uint32_t data_value);

  std::pair<std::uint32_t, bool> DecodeBlock(std::uint32_t codeword);
};

}  // namespace hamarc
