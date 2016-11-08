#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

// Call this to go into raw mode.
void start_keyboard();

// Call this to get our of raw mode. Must be paired with start_keyboard().
void stop_keyboard();

// Call this regularly.
void poll_keyboard();

// Returns the ASCII value in the lower 7 bits and the strobe in the 8th bit.
unsigned char get_keyboard_data_and_strobe();

// Clears the strobe and pretends that no keys are down.
unsigned char get_any_key_down_and_clear_strobe();

// Peek at the current key
bool peek_key(char *k);

// Peek at the current key
void clear_strobe();

#endif /* _KEYBOARD_H_ */
