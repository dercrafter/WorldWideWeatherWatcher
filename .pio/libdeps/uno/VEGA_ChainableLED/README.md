ChainableLED
============

Arduino library compatible with Grove Chainable LED and the P9813 chip. It allows controlling a chain of LEDS individually. 

Supports both RGB and HSL color spaces for setting the color of each individual LED.


[More information on the wiki](https://gitlab.com/riscv-vega/vega-sensor-libraries/display/vega_chainableled)


Installation
============
1. Open Arduino IDE & go to Tools >> Manage libraries... 
2. Search for `VEGA_ChainableLED`.
3. Select latest version and click on `Install`.

OR

1. Grab the latest version from the release section of GitHub.
(https://gitlab.com/riscv-vega/vega-sensor-libraries/display/vega_chainableled)

2. Unzip it to your Arduino "libraries" directory. 

3. It should be ready to use. Examples are included.


Library Interface
=================
```c++
class ChainableLED {
public:
  ChainableLED(byte clk_pin, byte data_pin, byte number_of_leds);

  void init();
  void setColorRGB(byte led, byte red, byte green, byte blue);
  void setColorHSL(byte led, float hue, float saturation, float lightness);
}
```
