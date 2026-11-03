/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "packet.h"

#include "common/net/endian.h"

#include "fmt/format.h"
#include <cctype>
#include <cstring>
#include <limits>

// Preserve the existing native scalar representation and packet wire widths.
static_assert(sizeof(int8_t) == 1 && sizeof(uint8_t) == 1, "Packet 8-bit scalar width");
static_assert(sizeof(int16_t) == 2 && sizeof(uint16_t) == 2, "Packet 16-bit scalar width");
static_assert(sizeof(int32_t) == 4 && sizeof(uint32_t) == 4, "Packet 32-bit scalar width");
static_assert(sizeof(int64_t) == 8 && sizeof(uint64_t) == 8, "Packet 64-bit scalar width");
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "Packet floating-point scalar widths");

void EQ::Net::Packet::PutInt8(size_t offset, int8_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutInt8(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutInt16(size_t offset, int16_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutInt16(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutInt32(size_t offset, int32_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutInt32(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutInt64(size_t offset, int64_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutInt64(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutUInt8(size_t offset, uint8_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutUInt8(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutUInt16(size_t offset, uint16_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutUInt16(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutUInt32(size_t offset, uint32_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutUInt32(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutUInt64(size_t offset, uint64_t value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutUInt64(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutFloat(size_t offset, float value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutFloat(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutDouble(size_t offset, double value)
{
	if (offset > std::numeric_limits<size_t>::max() - sizeof(value) ||
		(Length() < offset + sizeof(value) && !Resize(offset + sizeof(value)))) {
		throw std::out_of_range("Packet::PutDouble(), could not resize packet and would of written past the end.");
	}

	std::memcpy(static_cast<char *>(Data()) + offset, &value, sizeof(value));
}

void EQ::Net::Packet::PutString(size_t offset, const std::string &str)
{
	if (Length() < offset + str.length()) {
		if (!Resize(offset + str.length())) {
			throw std::out_of_range("Packet::PutString(), could not resize packet and would of written past the end.");
		}
	}

	memcpy(((char*)Data() + offset), str.c_str(), str.length());
}

void EQ::Net::Packet::PutCString(size_t offset, const char *str)
{
	size_t sz = strlen(str);
	if (Length() < offset + sz + 1) {
		if (!Resize(offset + sz + 1)) {
			throw std::out_of_range("Packet::PutCString(), could not resize packet and would of written past the end.");
		}
	}

	memcpy(((char*)Data() + offset), str, sz);
	*((char*)Data() + offset + sz) = 0;
}

void EQ::Net::Packet::PutPacket(size_t offset, const Packet &p)
{
	if (p.Length() == 0) {
		return;
	}

	if (Length() < offset + p.Length()) {
		if (!Resize(offset + p.Length())) {
			throw std::out_of_range("Packet::PutPacket(), could not resize packet and would of written past the end.");
		}
	}

	memcpy(((char*)Data() + offset), p.Data(), p.Length());
}

void EQ::Net::Packet::PutData(size_t offset, void *data, size_t length)
{
	if (length == 0) {
		return;
	}

	if (Length() < offset + length) {
		if (!Resize(offset + length)) {
			throw std::out_of_range("Packet::PutData(), could not resize packet and would of written past the end.");
		}
	}

	memcpy(((char*)Data() + offset), data, length);
}

int8_t EQ::Net::Packet::GetInt8(size_t offset) const
{
	int8_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

int16_t EQ::Net::Packet::GetInt16(size_t offset) const
{
	int16_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

int32_t EQ::Net::Packet::GetInt32(size_t offset) const
{
	int32_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

int64_t EQ::Net::Packet::GetInt64(size_t offset) const
{
	int64_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

uint8_t EQ::Net::Packet::GetUInt8(size_t offset) const
{
	uint8_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

uint16_t EQ::Net::Packet::GetUInt16(size_t offset) const
{
	uint16_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

uint32_t EQ::Net::Packet::GetUInt32(size_t offset) const
{
	uint32_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

uint64_t EQ::Net::Packet::GetUInt64(size_t offset) const
{
	uint64_t value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

float EQ::Net::Packet::GetFloat(size_t offset) const
{
	float value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

double EQ::Net::Packet::GetDouble(size_t offset) const
{
	double value;
	const auto length = Length();
	if (offset > length || length - offset < sizeof(value)) {
		throw std::out_of_range("Packet read out of range.");
	}

	std::memcpy(&value, static_cast<const char *>(Data()) + offset, sizeof(value));
	return value;
}

std::string EQ::Net::Packet::GetString(size_t offset, size_t length) const
{
	if (Length() < offset + length) {
		throw std::out_of_range("Packet read out of range.");
	}

	return std::string((char*)Data() + offset, (char*)Data() + offset + length);
}

std::string EQ::Net::Packet::GetCString(size_t offset) const
{
	if (Length() < offset + 1) {
		throw std::out_of_range("Packet read out of range.");
	}

	char *str = ((char*)Data() + offset);
	return std::string(str);
}

char ToSafePrint(unsigned char in) {
	if (std::isprint(in)) {
		return in;
	}

	return '.';
}

std::string EQ::Net::Packet::ToString() const
{
	return ToString(16);
}

std::string EQ::Net::Packet::ToString(size_t line_length) const
{
	if (Length() == 0) {
		return fmt::format("{:0>5x} |", 0);
	}

	std::string ret;
	size_t lines = Length() / line_length;
	size_t i;

	char *data = (char*)Data();

	for (i = 0; i < lines; ++i) {
		ret += fmt::format("{:0>5x} |", i * line_length);
		std::string hex;
		std::string ascii;
		for (size_t j = 0; j < line_length; ++j) {
			hex += fmt::format(" {:0>2x}", (uint8_t)data[(i * line_length) + j]);
			ascii += fmt::format("{}", ToSafePrint(data[(i * line_length) + j]));
		}

		ret += hex;
		ret += " | ";
		ret += ascii;
		ret += "\n";
	}

	if (Length() % line_length > 0) {
		ret += fmt::format("{:0>5x} |", i * line_length);

		size_t non_blank_count = Length() % line_length;
		size_t blank_count = line_length - non_blank_count;
		std::string hex;
		std::string ascii;

		for (size_t j = 0; j < non_blank_count; ++j) {
			hex += fmt::format(" {:0>2x}", (uint8_t)data[(i * line_length) + j]);
			ascii += fmt::format("{}", ToSafePrint(data[(i * line_length) + j]));
		}

		for (size_t j = 0; j < blank_count; ++j) {
			hex += "   ";
			ascii += " ";
		}

		ret += hex;
		ret += " | ";
		ret += ascii;
		ret += "\n";
	}

	return ret;
}

bool EQ::Net::StaticPacket::Resize(size_t new_size)
{
	if (new_size > m_max_data_length) {
		return false;
	}

	m_data_length = new_size;
	return true;
}
