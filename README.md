# Shiny Machine

The Shiny Machine is an ESP32-powered device that hunts shiny pokemon in the 7th-gen Pokemon games.

## Background

Shiny pokemon are significantly rarer versions of pokemon, with alternate color schemes and a unique intro animation when sent into battle. In the latest pokemon games, the base odds of encountering a shiny pokemon is 1/4096. By collecting  the Shiny Charm item in-game, players can increase their odds to 3/4096.

Players that hunt for shiny pokemon often set up their avatars in-game to re-trigger certain pokemon encounters every time they turn off and on their 3DS; this is called "soft resetting." For most pokemon encounters, soft resetting re-rolls the chance of a shiny pokemon being generated, so shiny hunters frequently soft reset their game hundreds to thousands of times by hand in order to find the shiny variant of their target pokemon.

I started this project to automate the chore of soft resetting for shiny pokemon. I found a few examples of people building shiny hunting machines that were exceedingly helpful when prototyping, so here are a few links:

- [/u/cabubaloo's shiny legendary auto hunter for the Nintendo Switch](https://old.reddit.com/r/ShinyPokemon/comments/aauks9/lgpe_shiny_legendary_auto_hunter_i_have_gotten/)
- [Nooby's Gamepro](https://www.noobysgamepro.com/)
- [/u/CaptainSpaceCat's shiny hunting device](https://www.reddit.com/r/pokemon/comments/7g6zbl/automatic_shiny_hunting_device/)
- [dekuNukem's Poke-O-Matic](https://www.youtube.com/watch?v=jyJPsZc-QTM)

Most importantly for this project was [this post by /u/Inigmatix](https://old.reddit.com/r/ShinyPokemon/comments/8igm15/talkobservation_from_making_a_shiny_resetting/). They explain that shiny pokemon have a battle intro animation in Pokemon Sun and Moon that lasts exactly 1.18 seconds. As pokemon encounters begin in these games, the lower screen of the 3DS transitions from dark, to a low level, to bright depending on the length of the encountered pokemon's battle intro animation. By measuring the time between these screen transitions, the presence of a shiny pokemon can be inferred. I happened to have the parts used by Inigmatix when I discovered their post, so my first iteration of the machine was a very similar Arduino-based device, down to the pile of books holding servos in place over the A and Start buttons. It wasn't long, however, that I started to come up with improvements.

## Basic operation modes

(flowcharts here)

Ambush encounter
Overworld encounter
Honey encounter

## Design

### Microcontroller

The heart of the Shiny Machine is a WEMOS LOLIN32 ESP32 board with a built in OLED screen. ESP32 microcontrollers can be programmed using the Arduino IDE, so it was easy to port my Arduino code to it. I wanted to use an ESP32 instead of an Arduino for the MCU was because ESP32s have built-in wifi.

When I ran my first few hunts with an Arduino as the MCU, the machine notified me that it found a shiny pokemon by pausing its operation and blinking an LED. Because the button-pushing servos were noisy, I moved the machine to my garage. However, this meant that I started compulsively stopping by the garage to see if the machine had found anything yet. By switching to the ESP32, I could have the machine display its status on a webpage on my home network. But then I started compulsively checking the webpage, so I ended up setting up a Twilio account and had the machine call their API. Now, I get a text from Twilio when the machine finds a shiny pokemon, so I don't have to think about it at all.

### Motors and sensors

I wanted the Shiny Machine to interact with my 2DS in a non-invasive way so that I could still use the handheld regularly on its own. This meant that the machine would need to interact with the screen and buttons in a way similar to a human player. I hooked up a luminosity sensor to watch the bottom screen, and buttons were pressed by rotating servo arms.

The SG-50 servos were functional but somewhat noisy and bulky, so I switched to solenoids driven by Darlington transistors. I started with [these 5V solenoids](https://www.adafruit.com/product/2776) from Adafruit, but found that I had to significantly over-volt(?) them to get the force necessary to press the buttons on the 2DS. In order to avoid burning my house down, I switched to [12V solenoids](https://www.adafruit.com/product/412). I'm powering them at 20V, which should be within tolerance.

The luminosity sensor I started the project with was a [TSL2561](https://www.sparkfun.com/products/retired/12055). It died during a demonstration, so I upgraded to an [APDS9301](https://www.sparkfun.com/products/14350). This had the interesting side effect of significantly improving my timing measurements; the TSL2561 often had a spread of about 50ms around the values I measured. With the APDS9301, mesurements were accurate within 10ms.

(picture of before vs after spreads)

### Software

