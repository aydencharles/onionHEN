#pragma once

#include <cstddef>
#include <string>

struct OnPressContext;
enum class OnPressResult;

void generate_usb_payload_xml(std::string &xml_buffer);
OnPressResult onpress_usb_payload(OnPressContext &ctx);
