## BLE connection Proof-of-concept


Goal: Connect to a Bluetooth device with a bluetooth pairing code, using the ESP32 Nimble library by h2zero.

Caveats:
Do not confuse the syntax! Check the repository with tool-calls if necessary [Nimble Library](https://github.com/h2zero/NimBLE-Arduino)


Style-Guide:
Since we want to create an POC and evaluate and learn the code, do NOT:

- Create "fluff" like error handling, unnecessary case matching etc.
- Do not write verbose code
- Do not put comments in the code 
- be ware of compilation errors -> common mistake ...marked 'override', but does not override...
Instead:
- be concise
- keep it simple!
- minimum neccessary code


## Tasks


### Printer Control via Bluetooth

We implement a GATT Server that controls the printer settings. We have a static webpage using Tailwind and vue3 via CDN. On the webpage we utilize Web-Bluetooth APIs to communicate with our GATT Server.

- Implement GATT Server on Device, that controls printer settings. We have to make sure that on-change the printer settings get updated
- GATT and Printer logic should reside in a separate file - but make sure to place the c files in /src/printer and adapt the MeshtasticBLELogger. Do not create a new .ino file.
- Create Static webpage
- Styling of the Webpage: Hacker looking, "dithering"-style, dark and green as color.
- Modern printer-page
- as a working reference webpage, see "reference.html" & "reference.ino"
- the server should be open to anyone - no need for extra security here

Futher instructions:

- Especially for the server side BLE connection, follow the reference implementation AS CLOSELY AS POSSIBLE
- Architectural clarification: Our device acts as CLIENT and SERVER in the BLE context. So we need to use the same NIMBLE instance.

Style Guide:

- For the website take these references

```css
:root {
    --color-dark: #141414;
    --color-neutral: #faf5f5;
    --color-primary: #00ff00;
    --color-secondary: #9673ff;
    --color-additional-01: #ff3719;
    --color-additional-02: #66f2ff;

    --color-primary-tint-01: #009900;
    --color-primary-tint-02: #00be00;
    --color-primary-tint-03: #00d300;
    --color-primary-tint-04: #00ea00;
    --color-primary-tint-05: #a3ff90;
    --color-primary-tint-06: #ccffbe;
    --color-primary-tint-07: #ebffe5;

    --color-secondary-tint-01: #4d2eed;
    --color-secondary-tint-02: #5c33f4;
    --color-secondary-tint-03: #7952fe;
    --color-secondary-tint-04: #b69dfe;
    --color-secondary-tint-05: #d4c4fe;
    --color-secondary-tint-06: #efe7ff;
}
```

### Wire up real printer commands

At the moment we only have mock-values we can control from the frontend. The goal is to control the REAL printer.
Check the implementation of the firmware at:

https://github.com/adafruit/Adafruit-Thermal-Printer-Library/blob/master/Adafruit_Thermal.cpp

There are several useful settings that should be controllable (heat, print density, feed, lineheight, font, inverse, strike, doublewidth, justify, bold...)

Think of an easy to use config object to keep the code small and clean. On updates we have to write those changes to the actual printer object (check implementation in our .ino file)

In the frontend these settings should have corresponding widgets. Here we should keep it grouped and simple. e.g. we could have a dropdown with "textdecorations" instead of many checkboxes with different styles.