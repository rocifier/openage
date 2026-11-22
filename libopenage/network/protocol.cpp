// Copyright 2026 the openage authors. See copying.md for legal info.

#include "protocol.h"

#include <limits>

#include "error/error.h"
#include "network/serialization.h"

namespace openage::network {

namespace {

bool is_known_message_type_value(uint16_t value) {
	return value >= static_cast<uint16_t>(MessageType::CLIENT_HELLO)
	    && value <= static_cast<uint16_t>(MessageType::CHAT_MESSAGE);
}

size_t checked_frame_size(const uint8_t *header) {
	if (read_u32(header) != FRAME_MAGIC) {
		throw Error{MSG(err) << "Invalid FVD network frame magic"};
	}

	auto message_type = read_u16(header + 8);
	if (not is_known_message_type_value(message_type)) {
		throw Error{MSG(err) << "Unknown FVD network message type " << message_type};
	}

	auto flags = read_u16(header + 10);
	if (flags != 0) {
		throw Error{MSG(err) << "Unsupported FVD network frame flags " << flags};
	}

	auto payload_size = read_u32(header + 12);
	if (payload_size > MAX_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "FVD network frame payload exceeds "
		                     << MAX_PAYLOAD_SIZE << " bytes"};
	}

	return FRAME_HEADER_SIZE + payload_size;
}

Frame decode_validated_frame(const uint8_t *bytes, size_t frame_size) {
	Frame result{};
	result.protocol_major = read_u16(bytes + 4);
	result.protocol_minor = read_u16(bytes + 6);
	result.message_type = static_cast<MessageType>(read_u16(bytes + 8));
	result.flags = read_u16(bytes + 10);
	result.session_id = read_u64(bytes + 16);
	result.sequence = read_u64(bytes + 24);
	result.payload.assign(bytes + FRAME_HEADER_SIZE, bytes + frame_size);
	return result;
}

} // namespace

bool is_known_message_type(MessageType message_type) {
	return is_known_message_type_value(static_cast<uint16_t>(message_type));
}

std::vector<uint8_t> encode_frame(const Frame &frame) {
	if (frame.flags != 0) {
		throw Error{MSG(err) << "Unsupported FVD network frame flags " << frame.flags};
	}
	if (not is_known_message_type(frame.message_type)) {
		throw Error{MSG(err) << "Unknown FVD network message type "
		                     << static_cast<uint16_t>(frame.message_type)};
	}
	if (frame.payload.size() > MAX_PAYLOAD_SIZE
	    || frame.payload.size() > std::numeric_limits<uint32_t>::max()) {
		throw Error{MSG(err) << "FVD network frame payload exceeds "
		                     << MAX_PAYLOAD_SIZE << " bytes"};
	}

	std::vector<uint8_t> result{};
	result.reserve(FRAME_HEADER_SIZE + frame.payload.size());
	append_u32(result, FRAME_MAGIC);
	append_u16(result, frame.protocol_major);
	append_u16(result, frame.protocol_minor);
	append_u16(result, static_cast<uint16_t>(frame.message_type));
	append_u16(result, frame.flags);
	append_u32(result, static_cast<uint32_t>(frame.payload.size()));
	append_u64(result, frame.session_id);
	append_u64(result, frame.sequence);
	result.insert(result.end(), frame.payload.begin(), frame.payload.end());
	return result;
}

Frame decode_frame(const std::vector<uint8_t> &bytes) {
	if (bytes.size() < FRAME_HEADER_SIZE) {
		throw Error{MSG(err) << "Incomplete FVD network frame header"};
	}

	auto frame_size = checked_frame_size(bytes.data());
	if (bytes.size() != frame_size) {
		throw Error{MSG(err) << "Expected one FVD network frame of " << frame_size
		                     << " bytes, received " << bytes.size()};
	}

	return decode_validated_frame(bytes.data(), frame_size);
}

std::vector<Frame> FrameParser::push(const uint8_t *bytes, size_t size) {
	if (size > 0 && bytes == nullptr) {
		throw Error{MSG(err) << "Cannot append null data to FVD frame parser"};
	}
	if (size == 0) {
		return {};
	}

	this->buffer.insert(this->buffer.end(), bytes, bytes + size);
	std::vector<Frame> result{};
	size_t offset{0};
	while (this->buffer.size() - offset >= FRAME_HEADER_SIZE) {
		auto frame_size = checked_frame_size(this->buffer.data() + offset);
		if (this->buffer.size() - offset < frame_size) {
			break;
		}

		result.push_back(decode_validated_frame(this->buffer.data() + offset, frame_size));
		offset += frame_size;
	}

	if (offset > 0) {
		this->buffer.erase(this->buffer.begin(), this->buffer.begin() + offset);
	}
	return result;
}

std::vector<Frame> FrameParser::push(const std::vector<uint8_t> &bytes) {
	return this->push(bytes.data(), bytes.size());
}

size_t FrameParser::buffered_size() const {
	return this->buffer.size();
}

} // namespace openage::network
