# Open issues

## Simulator rate labels and the rate test

DESIGN.md §9 says the `fast` and `slow` scenarios use 135 and 85 cpm, while
§10 asks for generator runs at 90, 110 and 130 cpm. The generator keeps the §9
defaults. `cprsim::Generator::setRate()` lets the unit test run the same
waveform at 90 and 130 cpm without changing the public scenario set.

## Simulator kg curve and the default kg bands

DESIGN.md §9 maps mm to kg with `kg = 0.2*mm + 0.0012*mm*mm`. With the default
kg bands from §2.2 (6 kg / 12 kg) the 35, 54 and 68 mm scenario peaks map to
about 8.5, 14.3 and 19.1 kg, so all three land in the Deep band. The generator
keeps the §9 formula for normal use. The ForceKg depth test uses
`Generator::setKgScale(0.7f)` so the kg-only stream exercises the documented
6 kg / 12 kg bands while the mm stream still uses the §9 depths. This is a test
harness adjustment only; the engine and default profile keep the §2 values.
