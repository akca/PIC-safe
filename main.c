#include "Includes.h"
#include "LCD.h"

unsigned char toggle_flag;
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
unsigned char after_blink;
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

  while (toggle_flag == 0) { // wait for re1 to be pushed & released
    RE1_task();
  }

  // 3 second delay using loops
  delay_1sec();
  delay_1sec();
  delay_1sec();

  ClearLCDScreen();
  WriteCommandToLCD(0x81); // Goto to the 2nd char of the first line
  WriteStringToLCD("Set a pin:####");
  PORTJ = 0x40;
  PORTH = 0xff;
  init_tmr0_interrupt();
  init_rb_interrupt();
  init_ADC();

  rbX = 6;
  for (int digitno = 0; digitno < 4; digitno++) {
    counter = 0;
    hash_flag = 1;
    while (!adc_finish) { // while(1)
      if (blink_flag == 1) {
        blink_digit(digitno);
        blink_flag = 0;
      }
    }

    WriteCommandToLCD(0x8B + digitno);
    WriteDataToLCD((int)'0' + ADC_value);

    adc_finish = 0;
    INTCONbits.RBIF = 0;
    INTCONbits.RBIE = 1; // enable rb interrupt
    rb6_push = 0;

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
      WriteStringToLCD("XXXX");
      WriteCommandToLCD(0xC0);
      WriteStringToLCD("Try after 20sec.");
      char suspend_start = counter_test_period;

      while (1) {
        display_remaining_time();

        if (suspend_start > 20) {
          if (suspend_start - counter_test_period >= 20)
            break;
        } else if (counter_test_period <= 0)
          break;
      }
    } else {
      WriteCommandToLCD(0x80);
      WriteStringToLCD("Safe is opening!");
      WriteCommandToLCD(0xC0);
      WriteStringToLCD("$$$$$$$$$$$$$$$$");

      while (1) {
        PIE1bits.TMR1IE = 0;
        display_remaining_time();
      }
    }
  }
  PIE1bits.ADIE = 0;
  ADCON0bits.ADON = 0;
  INTCONbits.PEIE = 0;
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

    

    INTCONbits.RBIE = 0;

    // check entered pin is correct
    int digit;
    for (digit = 0; digit < 4; digit++) {
      if (pin[digit] != entered_pin[digit])
        break;
    }

    // pin is correct
    if (digit == 4)
      return 1;
  }
  
  // no attempts left, fail
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
      toggle_flag = 1;
    }
    break;
  }
}

void init_system() {
  toggle_flag = 0;
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

void delay_10msec() // 10 millisecond of delay
{
  int t = 11110;

  while (t != 0) {
    t--;
  }
}

void init_tmr0_interrupt() {
  counter = 0;

  T0CON = 0b01000111;
  TMR0 = 0;

  INTCON2bits.TMR0IP = 1; // Timer0 set as HIGH priority
  INTCONbits.TMR0IF = 0;
  INTCONbits.TMR0IE = 1;
  T0CONbits.TMR0ON = 1;   // Enable Timer0 by setting TMR0ON
}

void init_rb_interrupt() {
  TRISB4 = 0;           // rb4 is output
  TRISB6 = 1;
  TRISB7 = 1;           // rb6 & rb7 - inputs
  INTCON2bits.RBIP = 1; // set rb as high-priority interrupt
  INTCON2bits.RBPU = 0; // pull-ups are enabled for rb6 & rb7 since
                        // they are configured as inputs
  PORTB;                // read PORTB value to end mismatch
  INTCONbits.RBIF = 0;
}

void init_ADC() {
  ADCON1 = 0;           // TODO: check other channels
  ADCON0 = 0;
  ADCON0 = ADCON0 | 0b00110000;
  ADCON2 = 0b10010010;
  ADCON0bits.ADON = 1;
  PIR1bits.ADIF = 0;
  IPR1bits.ADIP = 0;
  PIE1bits.ADIE = 1;
  INTCONbits.PEIE = 1;
}

void interrupt high_priority high_isr() {
  if (INTCONbits.TMR0IE && INTCONbits.TMR0IF) {
    counter++;           // increment counter variable
    counter2++;          // increment counter variable
    
    if (counter == 38) { // 250 ms
      counter = 0;
      blink_flag = 1;
      counter500ms++;
      if (counter500ms % 2 == 0) {
        _500ms_passed = 1;
      }
    }
    
    if (counter2 == 15) { // 100 ms
      counter2 = 0;
      ADCON0bits.GO = 1;
    }
    
    INTCONbits.TMR0IF = 0; // Clear TMROIF
    
  } else if (INTCONbits.RBIE && INTCONbits.RBIF) {

    preval = PORTB & (0x01 << rbX);

    // if (((PORTB & (0x01 << rbX)) == 0) == preval ) { // check if rb6/rb7 is
    // 0, i.e. pressed
    rb_int_received = 1;
    INTCONbits.RBIF = 0;
    
  } else if (PIE1bits.TMR1IE && PIR1bits.TMR1IF) {
    
    counter1sec++;
    
    if (counter1sec == 19) { // 1 second delay
      counter1sec = 0;
      counter_test_period--;
    }
    
    PIR1bits.TMR1IF = 0;
  }
}
void interrupt low_priority low_isr() {
  if (PIE1bits.ADIE && PIR1bits.ADIF) {
    ADC_old_value = ADC_value;
    ADC_value = map_ADC_value((ADRESH << 8) + ADRESL);

    if (ADC_old_value != ADC_value && !firstrun_flag) {
      adc_finish = 1;
    }

    firstrun_flag = 0;
    PIR1bits.ADIF = 0;
  }
}

void blink_digit(int digit_address) {

  if (hash_flag == 1) {
    WriteCommandToLCD(digit_address + 0x8B);
    after_blink = 1;

    if (display_active)
      display_remaining_time();

    WriteDataToLCD(' ');

    if (display_active)
      display_remaining_time();

    after_blink = 0;
    hash_flag = 0;

  } else if (hash_flag == 0) {

    WriteCommandToLCD(digit_address + 0x8B);
    after_blink = 1;

    if (display_active)
      display_remaining_time();

    WriteDataToLCD('#');

    if (display_active)
      display_remaining_time();

    after_blink = 0;
    hash_flag = 1;
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
