#include "hamming_codec.h"
#include "hamming_options.h"

#include <cstdint>
#include <iostream>
#include <utility>

namespace hamarc {
namespace {

unsigned int CalculateSyndrome(std::uint32_t codeword, int total_bits) {
  unsigned int syndrome = 0;

  for (int parity_position = 1; parity_position <= total_bits;
       parity_position <<= 1) {
    if (parity_position > total_bits) {
      break;
    }

    int parity_value = 0;
    for (int bit_index = 1; bit_index <= total_bits; ++bit_index) {
      if (bit_index & parity_position) {
        if (codeword & (1u << (bit_index - 1))) {
          parity_value ^= 1;
        }
      }
    }

    if (parity_value) {
      syndrome |= static_cast<unsigned int>(parity_position);
    }
  }

  return syndrome;
}

std::uint32_t ExtractDataBits(std::uint32_t codeword, int total_bits) {
  std::uint32_t data_value = 0;
  int data_index = 0;

  for (int bit_position = 1; bit_position <= total_bits; ++bit_position) {
    bool is_parity_position = (bit_position & (bit_position - 1)) == 0;
    if (is_parity_position) {
      continue;
    }

    int bit_value = (codeword >> (bit_position - 1)) & 1;
    if (bit_value) {
      data_value |= (1u << data_index);
    }
    ++data_index;
  }

  return data_value;
}

}  // namespace

HammingCodec::HammingCodec(const HammingOptions& opts)
    : data_bits_(opts.data_bits),
      parity_bits_(opts.parity_bits),
      total_bits_(opts.data_bits + opts.parity_bits) {}

bool HammingCodec::EncodeStream(std::istream& in, std::ostream& out) {
  if (!in.good() || !out.good()) {
    return false;
  }

  unsigned char input_byte = 0;
  std::uint32_t data_block = 0;
  int data_block_bits = 0;

  unsigned char output_byte = 0;
  int output_bits_in_byte = 0;

  while (in.read(reinterpret_cast<char*>(&input_byte), 1)) {
    for (int bit_index = 0; bit_index < 8; ++bit_index) {
      int bit_value = (input_byte >> bit_index) & 1;
      if (bit_value) {
        data_block |= (1u << data_block_bits);
      }
      ++data_block_bits;

      if (data_block_bits == data_bits_) {
        if (!EncodeAndWriteBlock(data_block, output_byte,
                                 output_bits_in_byte, out)) {
          return false;
        }
        data_block = 0;
        data_block_bits = 0;
      }
    }
  }

  if (!in.eof()) {
    return false;
  }

  if (data_block_bits > 0) {
    if (!EncodeAndWriteBlock(data_block, output_byte, output_bits_in_byte, out)) {
      return false;
    }
  }

  return FlushOutputByte(output_byte, output_bits_in_byte, out);
}

bool HammingCodec::DecodeStream(std::istream& in, std::ostream& out,
                                std::uint64_t original_size, std::uint64_t) {
  if (!in.good() || !out.good()) {
    return false;
  }

  const std::uint64_t original_bits = original_size * 8;
  if (original_bits == 0) {
    return true;
  }

  const std::uint64_t data_bits_u64 = static_cast<std::uint64_t>(data_bits_);
  const std::uint64_t codeword_bits = static_cast<std::uint64_t>(total_bits_);
  const std::uint64_t codeword_count =
      (original_bits + data_bits_u64 - 1) / data_bits_u64;
  const std::uint64_t total_code_bits = codeword_count * codeword_bits;

  unsigned char input_byte = 0;
  int input_bits_available = 0;

  std::uint64_t bits_read = 0;
  std::uint32_t codeword_value = 0;
  int codeword_bits_collected = 0;

  unsigned char output_byte = 0;
  int output_bits_filled = 0;
  std::uint64_t bits_written = 0;

  while (bits_read < total_code_bits) {
    int encoded_bit = 0;
    if (!ReadNextEncodedBit(in, input_byte, input_bits_available, encoded_bit)) {
      return false;
    }

    if (encoded_bit) {
      codeword_value |= (1u << codeword_bits_collected);
    }
    ++codeword_bits_collected;
    ++bits_read;

    if (codeword_bits_collected != total_bits_) {
      continue;
    }

    auto [decoded_data, has_error] = DecodeBlock(codeword_value);
    if (has_error) {
      std::cerr
          << "Decoding error: uncorrectable data corruption detected.\n";
      return false;
    }

    int bits_to_output = data_bits_;
    if (bits_written + bits_to_output > original_bits) {
      bits_to_output =
          static_cast<int>(original_bits - bits_written);
    }

    for (int bit_index = 0; bit_index < bits_to_output; ++bit_index) {
      int data_bit_value = (decoded_data >> bit_index) & 1;
      if (!WriteDecodedBitToStream(data_bit_value, output_byte,
                                   output_bits_filled, out)) {
        return false;
      }
    }

    bits_written += static_cast<std::uint64_t>(bits_to_output);
    codeword_value = 0;
    codeword_bits_collected = 0;
  }

  return FlushOutputByte(output_byte, output_bits_filled, out);
}

bool HammingCodec::EncodeAndWriteBlock(std::uint32_t data_block, unsigned char& out_byte,
                                       int& out_bit_count, std::ostream& out) {
  const std::uint32_t codeword = EncodeBlock(data_block);

  for (int bit_index = 0; bit_index < total_bits_; ++bit_index) {
    int code_bit = (codeword >> bit_index) & 1;
    if (code_bit) {
      out_byte |= (1u << out_bit_count);
    }
    ++out_bit_count;

    if (out_bit_count != 8) {
      continue;
    }

    out.put(static_cast<char>(out_byte));
    if (!out.good()) {
      return false;
    }
    out_byte = 0;
    out_bit_count = 0;
  }

  return true;
}

bool HammingCodec::FlushOutputByte(unsigned char out_byte,int out_bit_count, std::ostream& out) {
  if (out_bit_count == 0) {
    return true;
  }
  out.put(static_cast<char>(out_byte));
  return out.good();
}

bool HammingCodec::ReadNextEncodedBit(std::istream& in, unsigned char& in_byte,
                                      int& in_bits_available, int& bit_value) {
  if (in_bits_available == 0) {
    if (!in.read(reinterpret_cast<char*>(&in_byte), 1)) {
      return false;
    }
    in_bits_available = 8;
  }

  bit_value = in_byte & 1;
  in_byte >>= 1;
  --in_bits_available;
  return true;
}

bool HammingCodec::WriteDecodedBitToStream(int bit_value, unsigned char& out_byte,
                                           int& out_bit_fill, std::ostream& out) {
  if (bit_value) {
    out_byte |= (1u << out_bit_fill);
  }
  ++out_bit_fill;

  if (out_bit_fill != 8) {
    return true;
  }

  out.put(static_cast<char>(out_byte));
  if (!out.good()) {
    return false;
  }
  out_byte = 0;
  out_bit_fill = 0;
  return true;
}

std::uint32_t HammingCodec::EncodeBlock(std::uint32_t data_value) {
  std::uint32_t codeword = 0;
  int data_index = 0;

  for (int bit_position = 1; bit_position <= total_bits_; ++bit_position) {
    bool is_parity_position = (bit_position & (bit_position - 1)) == 0;
    if (is_parity_position) {
      continue;
    }

    int bit_value = (data_value >> data_index) & 1;
    if (bit_value) {
      codeword |= (1u << (bit_position - 1));
    }
    ++data_index;
  }

  for (int parity_position = 1; parity_position <= total_bits_;
       parity_position <<= 1) {
    if (parity_position > total_bits_) {
      break;
    }

    int parity_value = 0;
    for (int bit_index = 1; bit_index <= total_bits_; ++bit_index) {
      if (bit_index & parity_position) {
        if (codeword & (1u << (bit_index - 1))) {
          parity_value ^= 1;
        }
      }
    }

    if (parity_value) {
      codeword |= (1u << (parity_position - 1));
    }
  }

  return codeword;
}

std::pair<std::uint32_t, bool> HammingCodec::DecodeBlock(
    std::uint32_t codeword) {
  unsigned int syndrome = CalculateSyndrome(codeword, total_bits_);
  bool has_error = false;

  if (syndrome != 0U) {
    if (syndrome <= static_cast<unsigned int>(total_bits_)) {
      codeword ^= (1u << (syndrome - 1));
    } else {
      has_error = true;
    }
  }

  if (!has_error) {
    unsigned int verify_syndrome = CalculateSyndrome(codeword, total_bits_);
    if (verify_syndrome != 0U) {
      has_error = true;
    }
  }

  std::uint32_t data_value = 0;
  if (!has_error) {
    data_value = ExtractDataBits(codeword, total_bits_);
  }
  return {data_value, has_error};
}

}  // namespace hamarc
