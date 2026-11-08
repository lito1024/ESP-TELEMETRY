// ESP-038 D/E: raw register mirror records (ESPTelemetry.h).
// Build: python -m ziglang c++ -std=c++17 -Isrc tests/test_raw_registers.cpp -o test_raw_registers
#include <stdint.h>
#include <stdio.h>

#include "ESPTelemetry.h"

using namespace ESPTelemetry;

static int failures = 0;

static void expect(bool condition, const char *what)
{
    if (!condition)
    {
        ++failures;
        printf("FAIL: %s\n", what);
    }
}

int main()
{
    expect(rawRegisterFieldId(RawRegisterSpace::Holding, 0) == 0xE000, "holding 0");
    expect(rawRegisterFieldId(RawRegisterSpace::Holding, 248) == 0xE07C, "holding 248");
    expect(rawRegisterFieldId(RawRegisterSpace::Holding, 249) == 0xE07C, "odd address -> its pair");
    expect(rawRegisterFieldId(RawRegisterSpace::Input, 250) == 0xE47D, "input 250");
    expect(rawRegisterFieldId(RawRegisterSpace::Input, 2046) == 0xE7FF, "input 2046");
    expect(!isRawRegisterFieldId(FieldIdGridSourceVoltage), "catalog id is not raw");
    expect(!isRawRegisterFieldId(kRawRegisterFieldIdEnd), "end is exclusive");
    RawRegisterSpace space = RawRegisterSpace::Holding;
    uint16_t address = 0;
    expect(decodeRawRegisterFieldId(0xE47D, space, address) && space == RawRegisterSpace::Input && address == 250,
           "decode input 250");
    expect(decodeRawRegisterFieldId(0xE07C, space, address) && space == RawRegisterSpace::Holding && address == 248,
           "decode holding 248");
    expect(!decodeRawRegisterFieldId(1001, space, address), "decode rejects catalog id");
    const int32_t pair = packRawRegisterPair(0x0000, 0x8000);
    expect(static_cast<uint32_t>(pair) == kInvalidI32, "pair can equal the invalid sentinel");
    expect(rawRegisterPairWord(pair, 0) == 0x0000 && rawRegisterPairWord(pair, 1) == 0x8000, "sentinel words");
    expect(rawRegisterPairWord(packRawRegisterPair(0x1234, 0xABCD), 0) == 0x1234, "low word");
    expect(rawRegisterPairWord(packRawRegisterPair(0x1234, 0xABCD), 1) == 0xABCD, "high word");

    printf("%s\n", failures == 0 ? "raw register tests passed" : "raw register tests FAILED");
    return failures == 0 ? 0 : 1;
}
