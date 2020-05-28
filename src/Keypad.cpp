/*
||
|| @file Keypad.cpp
|| @version 3.2.0
|| @author Mark Stanley, Alexander Brevig, Tim Trzepacz
|| @contact mstanley@technologist.com, alexanderbrevig@gmail.com, github@softegg.com
||
|| @description
|| | This library provides a simple interface for using matrix
|| | keypads. It supports multiple keypresses while maintaining
|| | backwards compatibility with the old single key library.
|| | It also supports user selectable pins and definable keymaps.
|| #
||
|| @license
|| | This library is free software; you can redistribute it and/or
|| | modify it under the terms of the GNU Lesser General Public
|| | License as published by the Free Software Foundation; version
|| | 2.1 of the License.
|| |
|| | This library is distributed in the hope that it will be useful,
|| | but WITHOUT ANY WARRANTY; without even the implied warranty of
|| | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
|| | Lesser General Public License for more details.
|| |
|| | You should have received a copy of the GNU Lesser General Public
|| | License along with this library; if not, write to the Free Software
|| | Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
|| #
||
*/
#include "Keypad.h"

// <<constructor>> Allows custom keymap, pin configuration, and keypad sizes.
Keypad::Keypad(const byte *row, const byte *col, const byte numRows, const byte numCols): sizeKpd{numRows, numCols} {
	rowPins = row;
	columnPins = col;

	setDebounceTime(10);
	setHoldTime(500);
	keypadEventListener = 0;

	startTime = 0;
}

// Let the user define a keymap - assume the same row/column count as defined in constructor
void Keypad::begin(const char *userKeymap) {
    keymap = userKeymap;
    initRowPins();
    initColumnPins();
}

void Keypad::initRowPins() {
    for (byte r=0; r<sizeKpd.rows; r++) {
        pin_mode(rowPins[r], OUTPUT);
        pin_write(rowPins[r], HIGH);
    }
}

void Keypad::initColumnPins() {
    for (byte c=0; c<sizeKpd.columns; c++) {
        pin_mode(columnPins[c], INPUT_PULLUP);
    }
}

// Populate the key list.
bool Keypad::getKeys() {
	bool keyActivity = false;

	// Limit how often the keypad is scanned. This makes the loop() run 10 times as fast.
	if ( (millis()-startTime)>debounceTime ) {
		scanKeys();
		keyActivity = updateList();
		startTime = millis();
	}

	return keyActivity;
}

void Keypad::writeRowPre(byte n) {
    pin_write(rowPins[n], LOW);
}

void Keypad::writeRowPost(byte n) {
    pin_write(rowPins[n], HIGH);
}

bool Keypad::readRow(byte n) {
    return !pin_read(columnPins[n]);
}

// Private : Hardware scan
void Keypad::scanKeys() {
	// bitMap stores ALL the keys that are being pressed.
	for (byte r=0; r<sizeKpd.rows; r++) {
        // Begin column pulse output.
        writeRowPre(r);

		for (byte c=0; c<sizeKpd.columns; c++) {
            bitWrite(bitMap[r], c, readRow(c));
		}

		// End column pulse.
        writeRowPost(r);
	}
}

// Manage the list without rearranging the keys. Returns true if any keys on the list changed state.
bool Keypad::updateList() {

	byte emptyPos = 0xFF;

	// Delete any IDLE keys
	for (byte i=0; i < KEYPAD_LIST_MAX; i++) {
		if (key[i].kstate==IDLE) {
			key[i].kchar = KEYPAD_NO_KEY;
			key[i].kcode = -1;
			key[i].stateChanged = false;
		}

		if (emptyPos == 0xFF && key[i].kchar == KEYPAD_NO_KEY)
		    emptyPos = i;
	}

    uint32_t tmpTime = millis();

	// Add new keys to empty slots in the key list.
	for (byte r=0; r<sizeKpd.rows; r++) {
		for (byte c=0; c<sizeKpd.columns; c++) {
			boolean button = bitRead(bitMap[r],c);
			byte keyCode = r * sizeKpd.columns + c;
			char keyChar = keymap[keyCode];
			int idx = findInList(keyCode);

			if (idx >= 0) {
                // Key is already on the list so set its next state.
                nextKeyState(idx, button);
            } else if (button && emptyPos != 0xFF) {
                // Key is NOT on the list so add it.
                // If an empty slot was found or don't add key to list.
                key[emptyPos].kchar = keyChar;
                key[emptyPos].kcode = keyCode;
                key[emptyPos].kstate = IDLE;		// Keys NOT on the list have an initial state of IDLE.
                nextKeyState (emptyPos, button);

                emptyPos = 0xFF;
                for (byte i=0; i < KEYPAD_LIST_MAX; i++) {
                    if (key[i].kchar == KEYPAD_NO_KEY) {
                        emptyPos = i;
                        break;
                    }
                }
			}
		}
	}
    if (millis() - tmpTime > 0) Serial.printf("Keypad Time: %d\n", millis() - tmpTime);

	// Report if the user changed the state of any key.
	for (byte i=0; i < KEYPAD_LIST_MAX; i++) {
		if (key[i].stateChanged) return true;
	}

	return false;
}

// Private
// This function is a state machine but is also used for debouncing the keys.
void Keypad::nextKeyState(byte idx, boolean button) {
	key[idx].stateChanged = false;

	switch (key[idx].kstate) {
		case IDLE:
			if (button == KEYPAD_CLOSED) {
				transitionTo(idx, PRESSED);
				holdTimer = millis(); }		// Get ready for next HOLD state.
			break;
		case PRESSED:
			if ((millis()-holdTimer)>holdTime)	// Waiting for a key HOLD...
				transitionTo(idx, HOLD);
			else if (button == KEYPAD_OPEN)				// or for a key to be RELEASED.
				transitionTo(idx, RELEASED);
			break;
		case HOLD:
			if (button == KEYPAD_OPEN)
				transitionTo(idx, RELEASED);
			break;
		case RELEASED:
			transitionTo(idx, IDLE);
			break;
	}
}

// New in 2.1
bool Keypad::isPressed(char keyChar) {
	for (byte i=0; i < KEYPAD_LIST_MAX; i++) {
		if ( key[i].kchar == keyChar ) {
			if ( (key[i].kstate == PRESSED) && key[i].stateChanged )
				return true;
		}
	}
	return false;	// Not pressed.
}

// Search by character for a key in the list of active keys.
// Returns -1 if not found or the index into the list of active keys.
int8_t Keypad::findInList(char keyChar) {
	for (byte i=0; i < KEYPAD_LIST_MAX; i++) {
		if (key[i].kchar == keyChar) {
			return i;
		}
	}
	return -1;
}

// Search by code for a key in the list of active keys.
// Returns -1 if not found or the index into the list of active keys.
int8_t Keypad::findInList(byte keyCode) {
	for (byte i=0; i < KEYPAD_LIST_MAX; i++) {
		if (key[i].kcode == keyCode) {
			return i;
		}
	}
	return -1;
}

// New in 2.0
char Keypad::waitForKey() {
	char waitKey = KEYPAD_NO_KEY;

	while((waitKey = getKey()) == KEYPAD_NO_KEY ) {}	// Block everything while waiting for a keypress.

	return waitKey;
}

// Backwards compatibility function.
KeyState Keypad::getState() {
	return key[0].kstate;
}

// The end user can test for any changes in state before deciding
// if any variables, etc. needs to be updated in their code.
bool Keypad::keyStateChanged() {
	return key[0].stateChanged;
}

// The number of keys on the key list, key[KEYPAD_LIST_MAX], equals the number
// of bytes in the key list divided by the number of bytes in a Key object.
byte Keypad::numKeys() {
	return sizeof(key)/sizeof(Key);
}

// Minimum debounceTime is 1 mS. Any lower *will* slow down the loop().
void Keypad::setDebounceTime(uint debounce) {
	debounce<1 ? debounceTime=1 : debounceTime=debounce;
}

void Keypad::setHoldTime(uint hold) {
    holdTime = hold;
}

void Keypad::addEventListener(void (*listener)(char)){
	keypadEventListener = listener;
}

//adds a new event listener that is given the key and key state as parameters
//this way you can actually tell what is going on with the key that is being passed
//in without crawling through they internal data structures...
void Keypad::addStatedEventListener(void (*listener)(char, KeyState)){
	keypadStatedEventListener = listener;
}

void Keypad::transitionTo(byte idx, KeyState nextState) {
	key[idx].kstate = nextState;
	key[idx].stateChanged = true;

	// Calls keypadEventListener on any key that changes state.
    if (keypadEventListener!=NULL)  {
        keypadEventListener(key[idx].kchar);
    }
    //call the event listener that contains the key state, if available
    if (keypadStatedEventListener!=NULL)
    {
        keypadStatedEventListener(key[idx].kchar, nextState);
    }
}

/*
|| @changelog
|| | 3.3.0 2020-04-26 - Dimitris Zervas  : Add support for shift registers
|| | 3.2.0 2019-05-26 - Mark Stanley  : Fixed compatibility issue with the ESP8266 in waitForKey().
|| | 3.2.0 2015-12-30 - Mark Stanley  : Started using Travis CI
|| | 3.2.0 2015-08-23 - Tim Trzepacz  : Added Stated Event Listener
|| | 3.1.0 2015-06-16 - Mark Stanley  : Changed versioning scheme to comply with Arduino library.properties file.
|| | 3.1 2013-01-15 - Mark Stanley     : Fixed missing RELEASED & IDLE status when using a single key.
|| | 3.0 2012-07-12 - Mark Stanley     : Made library multi-keypress by default. (Backwards compatible)
|| | 3.0 2012-07-12 - Mark Stanley     : Modified pin functions to support Keypad_I2C
|| | 3.0 2012-07-12 - Stanley & Young  : Removed static variables. Fix for multiple keypad objects.
|| | 3.0 2012-07-12 - Mark Stanley     : Fixed bug that caused shorted pins when pressing multiple keys.
|| | 2.0 2011-12-29 - Mark Stanley     : Added waitForKey().
|| | 2.0 2011-12-23 - Mark Stanley     : Added the public function keyStateChanged().
|| | 2.0 2011-12-23 - Mark Stanley     : Added the private function scanKeys().
|| | 2.0 2011-12-23 - Mark Stanley     : Moved the Finite State Machine into the function getKeyState().
|| | 2.0 2011-12-23 - Mark Stanley     : Removed the member variable lastUdate. Not needed after rewrite.
|| | 1.8 2011-11-21 - Mark Stanley     : Added decision logic to compile WProgram.h or Arduino.h
|| | 1.8 2009-07-08 - Alexander Brevig : No longer uses arrays
|| | 1.7 2009-06-18 - Alexander Brevig : Every time a state changes the keypadEventListener will trigger, if set.
|| | 1.7 2009-06-18 - Alexander Brevig : Added setDebounceTime. setHoldTime specifies the amount of
|| |                                          microseconds before a HOLD state triggers
|| | 1.7 2009-06-18 - Alexander Brevig : Added transitionTo
|| | 1.6 2009-06-15 - Alexander Brevig : Added getState() and state variable
|| | 1.5 2009-05-19 - Alexander Brevig : Added setHoldTime()
|| | 1.4 2009-05-15 - Alexander Brevig : Added addEventListener
|| | 1.3 2009-05-12 - Alexander Brevig : Added lastUdate, in order to do simple debouncing
|| | 1.2 2009-05-09 - Alexander Brevig : Changed getKey()
|| | 1.1 2009-04-28 - Alexander Brevig : Modified API, and made variables private
|| | 1.0 2007-XX-XX - Mark Stanley : Initial Release
|| #
*/
