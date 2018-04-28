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
unsigned char rb6_push;
unsigned char rb7_push;
unsigned char adc_finish;
unsigned int ADC_value;
unsigned int ADC_old_value;
unsigned char _500ms_passed;
unsigned char display_pin;
unsigned char rbX;

void init_system();
void RE1_task();
void sec_delay();
void init_tmr0_interrupt();
void init_rb_interrupt();
void blink_digit(int);
void set_digit();
void init_ADC();
void interrupt high_priority high_isr();
void interrupt low_priority low_isr();
int map_ADC_value(int);
void show_new_pin(unsigned char pin[]);

void main(void) {
  unsigned char pin[4];

  init_system();

  WriteCommandToLCD(0x81); // Goto to the 2nd char of the first line
  WriteStringToLCD("$>Very  Safe<$");
  WriteCommandToLCD(0xC1);
  WriteStringToLCD("$$$$$$$$$$$$$$");

  while (toggle_flag == 0) { // wait for re1 to be pushed & released
    RE1_task();
  }

  // 3 second delay using loops
  sec_delay();
  sec_delay();
  sec_delay();

  ClearLCDScreen();
  WriteCommandToLCD(0x81); // Goto to the 2nd char of the first line
  WriteStringToLCD("Set a pin:####");
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
    }

    pin[digitno] = ADC_value;
    rb6_push = 0;
    rb7_push = 0;
      INTCONbits.RBIF = 0;
    INTCONbits.RBIE = 0; // disable rb interrupt
    __delay_ms(100);
  }

  rb6_push = 0;
  rb7_push = 0;
  rbX = 7;
  INTCONbits.RBIF = 0;
  INTCONbits.RBIE = 1;
  
  while (!rb7_push)
    ;

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

  while (1)
    ;

  set_digit();
}

void show_new_pin(unsigned char pin[]) {
  ClearLCDScreen();
  WriteCommandToLCD(0x81);
  WriteStringToLCD("The new pin is");
  WriteCommandToLCD(0xC3);
  WriteStringToLCD("---");

  for (int digitno = 0; digitno < 4; digitno++) {
    WriteDataToLCD((int)'0' + pin[digitno]);
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
}

void sec_delay() // 1 second delay
{
  unsigned char t1 = 0x50;
  unsigned char t2 = 0x90;
  unsigned char t3 = 0x1F;

  while (t3 != 0) {
    while (t2 != 0) {
      while (t1 != 0) {
        t1--;
      }
      t1--;
      t2--;
    }
    t2--;
    t3--;
  }
}

void init_tmr0_interrupt() {
  counter = 0;

  T0CON = 0b01000111; // T0CON = b'01000111'
  TMR0 = 0;

  INTCON2bits.TMR0IP = 1; // bsf INTCON2, 2
                          // Timer0 set as HIGH priority
  INTCONbits.TMR0IF = 0;
  INTCONbits.TMR0IE = 1;
  // INTCONbits.PEIE = 1;

  T0CONbits.TMR0ON = 1; // Enable Timer0 by setting TMR0ON
}

void init_rb_interrupt() {
  PORTB = 0;
  TRISB4 = 0;
  TRISB5 = 0;
  TRISB6 = 1;
  TRISB7 = 1;
  INTCON2bits.RBIP = 1; // set rb as high-priority interrupt
  INTCON2bits.RBPU = 0; // pull-ups are enabled

  INTCONbits.RBIF = 0;
}

void interrupt high_priority high_isr() {
  if (INTCONbits.TMR0IE && INTCONbits.TMR0IF) {
    INTCONbits.TMR0IF = 0; // Clear TMROIF

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
    if (counter2 == 25) { // TODO should be adjusted to get 100 ms
      counter2 = 0;
      ADCON0bits.GO = 1;
    }
  } else if (INTCONbits.RBIE && INTCONbits.RBIF) {
    if ((PORTB & (0x01 << rbX)) != 0) {
      if (rbX == 6)
        rb6_push = 1;
      else if (rbX == 7)
        rb7_push = 1;
    }
    INTCONbits.RBIF = 0;
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

    //        WriteCommandToLCD(0x81);    // Goto to the 2nd char of the first
    //        line WriteDataToLCD((int)'0' + map_ADC_value(ADC_value));
  }
}

void blink_digit(int digit) {
  int digit_address = 0;
  switch (digit) {
  case 0:
    digit_address = 0x8B;
    break;
  case 1:
    digit_address = 0x8C;
    break;
  case 2:
    digit_address = 0x8D;
    break;
  case 3:
    digit_address = 0x8E;
  }
  if (hash_flag == 1) {
    WriteCommandToLCD(digit_address);
    WriteDataToLCD(' ');
    hash_flag = 0;
  } else if (hash_flag == 0) {
    WriteCommandToLCD(digit_address);
    WriteDataToLCD('#');
    hash_flag = 1;
  }
}

void set_digit() {}

void init_ADC() {
  ADCON1 = 0; // TODO: check other channels
  ADCON0 = 0;
  ADCON0 = ADCON0 | 0b00110000;
  ADCON2 = 0b10010010;
  ADCON0bits.ADON = 1;
  PIR1bits.ADIF = 0;
  IPR1bits.ADIP = 0;
  PIE1bits.ADIE = 1;
  INTCONbits.PEIE = 1;
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