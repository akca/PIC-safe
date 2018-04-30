//////////////////////////////////////////////////////////////////////
//                                                                  //
// CENG336 - Embedded Systems                                       //
// Take Home Exam 3                                                 //
//                                                                  //
// Group 66                                                         //
// 2098713 - Fatih Akca                                             //
// 2177269 - Narmin Aliyeva                                         //
//                                                                  //
// 30/04/2018                                                       //
//                                                                  //
//////////////////////////////////////////////////////////////////////
//                                                                  //
// This program is written for PIC18F8722 working at 40 MHz.        //
//                                                                  //
// The pin-setting period can be entered after the press and        //
// release of the RE1 button. This event is observed using the      //
// RE1_task() function which monitors the previous state of the     //
// button, i.e. pressed or not pressed.                             //
//                                                                  //
// Timer0, Timer1, and PORTB interrupts are handled inside the      //
// high_isr() function, since these are configured as high-priority //
// interrupts. Timer0 is configured to operate in 8-bit mode with a //
// prescale value of 1:256. Timer1 is configured to operate in      //
// 16-bit mode with a prescale value of 1:8. PORTB pull-ups are     //
// enabled for RB6 and RB7.                                         //
//                                                                  //
// ADC interrupt is handled inside the low_isr() function, since it //
// is configured as a low-priority interrupt. The isr checks if the //
// user is changing the digit value, if so, it triggers the display //
// action for the newly entered value on the LCD.                   //
//                                                                  //
//////////////////////////////////////////////////////////////////////

#include "Includes.h"
#include "LCD.h"

unsigned char RE1_event;
unsigned char RE1_state;
unsigned char hash_flag;
unsigned char blink_flag;
unsigned char firstrun_flag;
unsigned char counter;
unsigned char counter2;
unsigned char counter500ms;
unsigned char counter1sec;
unsigned char rb6_push;
unsigned char rb7_push;
unsigned char adc_finish;
unsigned int ADC_value;
unsigned int ADC_old_value;
unsigned char _500ms_passed;
unsigned char display_pin;
unsigned char rbX;
unsigned char preval;
unsigned char rb_int_received;
unsigned char pin[4];
unsigned char display_active;
char counter_test_period;

void init_system();
void RE1_task();
void delay_1sec();
void delay_4p5msec();
void delay_10msec();
void init_tmr0_interrupt();
void init_rb_interrupt();
void init_ADC();
void blink_digit(int);
void interrupt high_priority high_isr();
void interrupt low_priority low_isr();
int map_ADC_value(int);
void show_new_pin(unsigned char pin[]);
unsigned char enter_pin();
void display_remaining_time();

void main(void) {

  init_system();

  WriteCommandToLCD(0x81); // Goto to the 2nd char of the first line
  WriteStringToLCD("$>Very  Safe<$");
  WriteCommandToLCD(0xC1);
  WriteStringToLCD("$$$$$$$$$$$$$$");

  // wait for re1 to be pushed & released
  while (RE1_event == 0) {
    RE1_task();
  }

  // 3 second delay using loops
  delay_1sec();
  delay_1sec();
  delay_1sec();

  ClearLCDScreen();
  WriteCommandToLCD(0x81);
  WriteStringToLCD("Set a pin:####");
  PORTJ = 0x40;
  PORTH = 0xff;
  init_tmr0_interrupt();
  init_rb_interrupt();
  init_ADC();

  rbX = 6;
  for (int digitno = 0; digitno < 4; digitno++) {
    counter = 0; // clear timer0 counter for 250ms
    hash_flag = 1; // initially '#' is displayed

    // wait until a/d conversion is finished
    while (!adc_finish) {
      if (blink_flag == 1) {
        blink_digit(digitno);
        blink_flag = 0;
      }
    }

    // display the entered value on the corresponding LCD cell
    WriteCommandToLCD(0x8B + digitno);
    WriteDataToLCD((int)'0' + ADC_value);

    adc_finish = 0;
    INTCONbits.RBIF = 0;
    INTCONbits.RBIE = 1; // enable rb interrupt
    rb6_push = 0;

    // user adjusts the digit value then pushes rb6 button to set it
    while (rb6_push == 0) {
      if (adc_finish) {
        WriteCommandToLCD(0x8B + digitno);
        WriteDataToLCD((int)'0' + ADC_value);
        adc_finish = 0;
      }

      if (rb_int_received) {
        delay_10msec(); // debouncing
        if (preval == (PORTB & (0x01 << rbX)) && preval == 0) {
          if (rbX == 6)
            rb6_push = 1;
          else if (rbX == 7)
            rb7_push = 1;
        }
        rb_int_received = 0;
      }
    }

    pin[digitno] = ADC_value;
    INTCONbits.RBIE = 0; // disable rb interrupt
  }

  preval = 0;
  rb7_push = 0;
  rbX = 7;
  INTCONbits.RBIF = 0;
  INTCONbits.RBIE = 1;

  // wait for rb7 button push
  while (!rb7_push) {
    if (rb_int_received) {
      delay_10msec(); // debouncing
      if (preval == (PORTB & (0x01 << rbX)) && preval == 0) {
        if (rbX == 6)
          rb6_push = 1;
        else if (rbX == 7)
          rb7_push = 1;
      }
      rb_int_received = 0;
    }
  }

  INTCONbits.RBIE = 0;
  display_pin = 1;

  counter = 0;
  counter500ms = 0;
  // blink the message with 500 ms intervals for 3sec
  while (1) {
    if (_500ms_passed) {
      if (display_pin) {
        show_new_pin(pin);
        display_pin = 0;
      } else {
        ClearLCDScreen();
        display_pin = 1;
      }
      _500ms_passed = 0;
    }
    if (counter500ms == 12)
      break;
  }

  counter_test_period = 120;
  T1CON = 0;
  TMR1 = 0;
  T1CONbits.RD16 = 1;    // 16-bit mode
  T1CONbits.T1CKPS0 = 1; // 1:8 prescaler
  T1CONbits.T1CKPS1 = 1;
  IPR1bits.TMR1IP = 1;   // high priority
  PIR1bits.TMR1IF = 0;
  PIE1bits.TMR1IE = 1;
  T1CONbits.TMR1ON = 1;

  while (counter_test_period > 0) {

    if (!enter_pin()) {
      if (counter_test_period <= 0)
        break;

      WriteCommandToLCD(0x8B);
      WriteStringToLCD("XXXX"); // replace '#'s with 'X's
      WriteCommandToLCD(0xC0);
      WriteStringToLCD("Try after 20sec.");
      char suspend_start = counter_test_period;

      // counter keeps decrementing during this period
      while (1) {
        display_remaining_time();

        if (suspend_start > 20) {
          if (suspend_start - counter_test_period >= 20)
            break;
        } else if (counter_test_period <= 0)
          break;
      }
    } else { // if the pin is correctly entered
      WriteCommandToLCD(0x80);
      WriteStringToLCD("Safe is opening!");
      WriteCommandToLCD(0xC0);
      WriteStringToLCD("$$$$$$$$$$$$$$$$");

      while (1) {
	// timer1 interrupt disabled; counter stops decreasing
        PIE1bits.TMR1IE = 0;
        display_remaining_time();
      }
    }
  }

  PIE1bits.ADIE = 0;
  ADCON0bits.ADON = 0;
  INTCONbits.PEIE = 0;
}

void init_system() {
  RE1_event = 0;
  RE1_state = 0;

  InitLCD(); // Initialize LCD in 4bit mode

  PORTE = 0;
  TRISE1 = 1; // RE1 - input
  TRISH4 = 1; // input for ADC

  // disable all interrupts
  INTCON = 0;
  INTCON2 = 0;

  RCONbits.IPEN = 1;  // enable interrupt priorities
  INTCONbits.GIE = 1; // Enable Global, peripheral, Timer0

  firstrun_flag = 1;
  PORTH &= 0x0F;
}

void init_tmr0_interrupt() {
  counter = 0;

  T0CON = 0b01000111; // 8-bit mode, 1:256 prescale
  TMR0 = 0;

  INTCON2bits.TMR0IP = 1; // Timer0 set as HIGH priority
  INTCONbits.TMR0IF = 0;
  INTCONbits.TMR0IE = 1;
  T0CONbits.TMR0ON = 1; // Enable Timer0 by setting TMR0ON
}

void init_rb_interrupt() {
  TRISB4 = 0; // rb4 is output
  TRISB6 = 1;
  TRISB7 = 1;           // rb6 & rb7 - inputs
  INTCON2bits.RBIP = 1; // set rb as high-priority interrupt
  INTCON2bits.RBPU = 0; // pull-ups are enabled for rb6 & rb7 since
                        // they are configured as inputs
  PORTB;                // read PORTB value to end mismatch
  INTCONbits.RBIF = 0;
}

void init_ADC() {
  ADCON1 = 0;
  ADCON0 = 0;
  ADCON0 = ADCON0 | 0b00110000; // select AN12 for potentiometer
  ADCON2 = 0b10010010; // right justified, 4 Tad, Fosc/32
  ADCON0bits.ADON = 1;
  PIR1bits.ADIF = 0;
  IPR1bits.ADIP = 0;
  PIE1bits.ADIE = 1;
  INTCONbits.PEIE = 1;
}

unsigned char enter_pin() {
  display_active = 1;
  unsigned char entered_pin[4];

  ClearLCDScreen();
  WriteCommandToLCD(0xC2);
  WriteStringToLCD("Attempts:");

  for (unsigned char attempts = 2; attempts > 0; attempts--) {

    ADCON0bits.ADON = 1;
    PIR1bits.ADIF = 0;
    PIE1bits.ADIE = 1;
    INTCONbits.PEIE = 1;

    WriteCommandToLCD(0x81);
    WriteStringToLCD("Enter pin:####");
    WriteCommandToLCD(0xCB);
    WriteDataToLCD('0' + attempts);

    rbX = 6;
    for (int digitno = 0; digitno < 4; digitno++) {
      counter = 0;
      hash_flag = 1;
      adc_finish = 0;

      while (!adc_finish) {
        display_remaining_time();

        if (blink_flag == 1) {
          blink_flag = 0;
          blink_digit(digitno);
        }

        if (counter_test_period <= 0)
          return 0;
      }

      WriteCommandToLCD(0x8B + digitno);
      WriteDataToLCD((int)'0' + ADC_value);

      adc_finish = 0;
      INTCONbits.RBIF = 0;
      INTCONbits.RBIE = 1; // enable rb interrupt
      rb6_push = 0;

      while (rb6_push == 0) {
        display_remaining_time();

        if (counter_test_period <= 0)
          return 0;
        if (adc_finish) {
          WriteCommandToLCD(0x8B + digitno);
          WriteDataToLCD((int)'0' + ADC_value);
          adc_finish = 0;
        }

        if (rb_int_received) {
          delay_10msec(); // debouncing
          if (preval == (PORTB & (0x01 << rbX)) && preval == 0) {
            if (rbX == 6)
              rb6_push = 1;
            else if (rbX == 7)
              rb7_push = 1;
          }
          rb_int_received = 0;
        }
      }

      entered_pin[digitno] = ADC_value;
      INTCONbits.RBIE = 0; // disable rb interrupt
    }

    preval = 0;
    PIE1bits.ADIE = 0;
    ADCON0bits.ADON = 0;
    INTCONbits.PEIE = 0;
    rb7_push = 0;
    rbX = 7;
    INTCONbits.RBIF = 0;
    INTCONbits.RBIE = 1;

    while (!rb7_push) {
      display_remaining_time();

      if (counter_test_period <= 0)
        return 0;
      if (rb_int_received) {
        delay_10msec(); // debouncing
        if (preval == (PORTB & (0x01 << rbX)) && preval == 0) {
          if (rbX == 6)
            rb6_push = 1;
          else if (rbX == 7)
            rb7_push = 1;
        }
        rb_int_received = 0;
      }
    }

    // disable rb interrupt
    INTCONbits.RBIE = 0;

    // check entered pin is correct
    int digit;
    for (digit = 0; digit < 4; digit++) {
      if (pin[digit] != entered_pin[digit])
        break;
    }

    // pin is correct, return to main
    if (digit == 4)
      return 1;
  }

  // inform main of two consecutive unsuccessful attempts
  return 0;
}

void show_new_pin(unsigned char pin[]) {
  ClearLCDScreen();
  WriteCommandToLCD(0x81);
  WriteStringToLCD("The new pin is");
  WriteCommandToLCD(0xC3);
  WriteStringToLCD("---");

  for (int digitno = 0; digitno < 4; digitno++) {
    WriteDataToLCD('0' + pin[digitno]);
  }
  WriteStringToLCD("---");
}

void interrupt high_priority high_isr() {

  if (INTCONbits.TMR0IE && INTCONbits.TMR0IF) {
    // tmr0 interrupt handler

    counter++;  // increment counter variable
    counter2++;

    if (counter == 38) { // 250 ms
      counter = 0;
      blink_flag = 1;
      counter500ms++;
      if (counter500ms % 2 == 0) { // 500 ms
        _500ms_passed = 1;
      }
    }

    if (counter2 == 15) { // 100 ms
      counter2 = 0;
      ADCON0bits.GO = 1; // start a/d conversion
    }

    INTCONbits.TMR0IF = 0; // Clear TMROIF

  } else if (INTCONbits.RBIE && INTCONbits.RBIF) {
    // rb interrupt handler

    // store the observed rbX(rb6/rb7) state
    preval = PORTB & (0x01 << rbX);

    rb_int_received = 1;
    INTCONbits.RBIF = 0;

  } else if (PIE1bits.TMR1IE && PIR1bits.TMR1IF) {
    // tmr1 interrupt handler

    counter1sec++;

    if (counter1sec == 19) { // 1 second delay
      counter1sec = 0;
      counter_test_period--;
    }

    PIR1bits.TMR1IF = 0;
  }
}

void interrupt low_priority low_isr() {

  // a/d converter interrupt handler
  if (PIE1bits.ADIE && PIR1bits.ADIF) {
    ADC_old_value = ADC_value;
    ADC_value = map_ADC_value((ADRESH << 8) + ADRESL);

    // detect if the user is entering a new value
    if (ADC_old_value != ADC_value && !firstrun_flag) {
      adc_finish = 1;
    }

    firstrun_flag = 0;
    PIR1bits.ADIF = 0;
  }
}

void blink_digit(int digit_address) {

  if (hash_flag == 1) { // previously displaying '#'
    WriteCommandToLCD(digit_address + 0x8B);

    if (display_active)
      display_remaining_time();

    WriteDataToLCD(' ');

    if (display_active)
      display_remaining_time();

    hash_flag = 0;

  } else if (hash_flag == 0) { // previously displaying ' '

    WriteCommandToLCD(digit_address + 0x8B);

    if (display_active)
      display_remaining_time();

    WriteDataToLCD('#');

    if (display_active)
      display_remaining_time();

    hash_flag = 1;
  }
}

void display_remaining_time() {

  PORTJ = 0x00;
  PORTH = 0xff;
  int divisor = 1000;
  unsigned char value = counter_test_period;
  unsigned char tmp;

  for (int digit = 0; digit < 4; digit++) {

    value = value / divisor;

    switch (value) {
    case 0:
      tmp = 0b00111111;
      break;
    case 1:
      tmp = 0b00000110;
      break;
    case 2:
      tmp = 0b01011011;
      break;
    case 3:
      tmp = 0b01001111;
      break;
    case 4:
      tmp = 0b01100110;
      break;
    case 5:
      tmp = 0b01101101;
      break;
    case 6:
      tmp = 0b01111101;
      break;
    case 7:
      tmp = 0b00000111;
      break;
    case 8:
      tmp = 0b01111111;
      break;
    case 9:
      tmp = 0b01101111;
      break;
    }

    PORTH &= 0b11110000;
    PORTH |= 0x01 << digit;
    PORTJ = tmp;

    delay_4p5msec(); // 7 segment display flicker avoidance

    value = counter_test_period % divisor;
    divisor /= 10;
  }
}

int map_ADC_value(int value) {
  if (value < 100) {
    return 0;
  } else if (value < 200) {
    return 1;
  } else if (value < 300) {
    return 2;
  } else if (value < 400) {
    return 3;
  } else if (value < 500) {
    return 4;
  } else if (value < 600) {
    return 5;
  } else if (value < 700) {
    return 6;
  } else if (value < 800) {
    return 7;
  } else if (value < 900) {
    return 8;
  } else {
    return 9;
  }
}

void RE1_task() {
  switch (RE1_state) {
  case 0: // previously not pressed
    if (PORTE & 0x02) {
      RE1_state = 1;
    }
    break;

  case 1: // previously pressed
    if (!(PORTE & 0x02)) {
      RE1_state = 0;
      RE1_event = 1;
    }
    break;
  }
}

void delay_1sec() // 1 second of delay
{
  int t1 = 1000;
  int t2 = 1248;

  while (t1 != 0) {
    while (t2 != 0) {
      t2--;
    }
    t1--;
    t2 = 1248;
  }
}

void delay_4p5msec() // 4.5 milliseconds of delay
{
  int t = 4999;

  while (t != 0) {
    t--;
  }
}

void delay_10msec() // 10 milliseconds of delay
{
  int t = 11110;

  while (t != 0) {
    t--;
  }
}
