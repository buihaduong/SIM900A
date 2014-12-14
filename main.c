#include <msp430g2553.h>

#define	LED_RED	BIT0
#define LED_GREEN	BIT6

#define	SIM_POW	BIT0

#define LED_ON(x)	P1OUT|=x
#define LED_OFF(x)	P1OUT&=~(x)

void uart_putc(unsigned char c) {
	while (!(IFG2 & UCA0TXIFG))
		;
	UCA0TXBUF = c;
}

void uart_puts(unsigned char *data) {
	while (*data)
		uart_putc(*data++);
	return;
}

void delay_us(unsigned int us) {
	while (us) {
		__delay_cycles(1); // 1 for 1 Mhz set 16 for 16 MHz
		us--;
	}
}

void delay_ms(unsigned int ms) {
	while (ms) {
		__delay_cycles(1000); //1000 for 1MHz and 16000 for 16MHz
		ms--;
	}
}

void blink_led(char led, char t) {
	char i = 0;
	for (i = 0; i < t; i++) {
		LED_ON(led);
		delay_ms(100);
		LED_OFF(led);
		delay_ms(100);
	}
}

void power_sim(void) {
	P2OUT |= SIM_POW;
	delay_ms(2000);
	P2OUT &= ~SIM_POW;
	delay_ms(10000);
	blink_led(LED_GREEN, 2);
}

unsigned char isSendSMS = 0;

unsigned char data[128];
unsigned char index_data = 0;

unsigned char data_prev = 0;
unsigned char data_current = 0;

unsigned char count_eln = 0;
unsigned char isGetData = 0;
unsigned char doneGetData = 0;

unsigned char isHasSMS = 0;
unsigned char isSimSendData = 0;
unsigned char reic[128];
unsigned char index_reic = 0;

void start_get_data(void) {
	data_prev = 0;
	data_current = 0;
	count_eln = 0;
	isGetData = 1;
	doneGetData = 0;
	index_data = 0;
}

void complete_get_data(void) {
	isGetData = 0;
	doneGetData = 1;
}

unsigned char check_ok(void) {
	unsigned char i = 0;
	for (i = 0; i < index_data - 1; i++)
		if (data[i] == 'O' && data[i + 1] == 'K')
			return 1;
	return 0;
}

unsigned char check_status_sim(void) {
	uart_puts("at\r\n");
	start_get_data();
	while (!doneGetData)
		;
	doneGetData = 0;
	return check_ok();
}

unsigned char send_sms(unsigned char *phone, unsigned char *message) {
	uart_puts("AT+CMGS=\"");
	uart_puts(phone);
	uart_puts("\"\r\n");
	start_get_data();
	while (!doneGetData)
		;
	doneGetData = 0;
	unsigned char i = 0;
	for (i = 0; i < index_data; i++)
		if (data[i] == '>') {
			uart_puts(message);
			uart_putc(0x1a);
		}
	start_get_data();
	while (!doneGetData)
		;
	doneGetData = 0;
	blink_led(LED_RED,3);
	return check_ok();
}

void check_sms(void) {
	unsigned char i = 0;
	unsigned char k = 0;
	unsigned char phone[15];

	i = 3;
	while (i < 15) {
		if (reic[i] == '"')
			break;
		phone[k] = reic[i];
		k++;
		i++;
	}
	phone[k] = '\0';
	send_sms(phone,"OK Duong");
}

int main(void) {
	WDTCTL = WDTPW + WDTHOLD;                 // Stop WDT

	/* Use Calibration values for 1MHz Clock DCO*/
	DCOCTL = 0;
	BCSCTL1 = CALBC1_1MHZ;
	DCOCTL = CALDCO_1MHZ;

	/* Configure Pin Muxing P1.1 RXD and P1.2 TXD */
	P1SEL |= BIT1 | BIT2;
	P1SEL2 |= BIT1 | BIT2;

	/* Place UCA0 in Reset to be configured */
	UCA0CTL1 = UCSWRST;

	/* Configure */
	UCA0CTL1 |= UCSSEL_2;                     // SMCLK
	UCA0BR0 = 104;                            // 1MHz 9600
	UCA0BR1 = 0;                              // 1MHz 9600
	UCA0MCTL = UCBRS0;                        // Modulation UCBRSx = 1

	/* Take UCA0 out of reset */
	UCA0CTL1 &= ~UCSWRST;

	/* Enable USCI_A0 RX interrupt */
	IE2 |= UCA0RXIE;

	/* Enable leds*/
	P1DIR |= LED_GREEN + LED_RED;
	LED_OFF(LED_GREEN|LED_RED);

	/* Blink leds when reset */
	blink_led(LED_RED | LED_GREEN, 5);

	/* Init Button */
	P1REN |= BIT3;                   // Enable internal pull-up/down resistors
	P1OUT |= BIT3;                   //Select pull-up mode for P1.3
	P1IE |= BIT3;                       // P1.3 interrupt enabled
	P1IES |= BIT3;                     // P1.3 Hi/lo edge
	P1IFG &= ~BIT3;                  // P1.3 IFG cleared

	/* Power up Sim900A */
	P2DIR |= SIM_POW;
	power_sim();

	__enable_interrupt();

	if (check_status_sim())
		blink_led(LED_GREEN, 5);
	else {
		blink_led(LED_RED, 5);
	}

	while (1) {
		if (isSendSMS) {
			isSendSMS = 0;
			send_sms("0934384878", "Hello Duong");
		}
		if (isHasSMS) {
			blink_led(LED_GREEN,3);
			check_sms();
			isHasSMS = 0;
			isSimSendData = 0;
			index_reic = 0;
			data_prev = 0;
			data_current = 0;
		}
	}
}

/*  Echo back RXed character, confirm TX buffer is ready first */
#pragma vector=USCIAB0RX_VECTOR
__interrupt void USCI0RX_ISR(void) {
//	UCA0TXBUF = UCA0RXBUF;                    // TX -> RXed character
//	data[index] = UCA0RXBUF;
//	index++;
	data_prev = data_current;
	data_current = UCA0RXBUF;
	if (isGetData) {
		if (count_eln == 2) {
			data[index_data] = data_current;
			if ((data[index_data] == 0x0A) || (data[index_data] == '>'))
				complete_get_data();
			index_data++;
		} else {
			if ((data_prev == 0x0D) && (data_current == 0x0A))
				count_eln++;
		}
	} else if (!isHasSMS) {
		reic[index_reic] = data_current;
		index_reic++;
		if (index_reic > 2 && isSimSendData == 0) {
			if ((reic[index_reic - 3] == 'C') && (reic[index_reic - 2] == 'M')
					&& (reic[index_reic - 1] == 'T')) {
				isSimSendData = 1;
				index_reic = 0;
				count_eln = 0;
			}
		} else if (isSimSendData) {
			if ((data_prev == 0x0D) && (data_current == 0x0A))
				count_eln++;
			if (count_eln > 1) {
				isHasSMS = 1;
			}
		}
	}
}

#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void) {
	blink_led(LED_GREEN, 3);
	isSendSMS = 1;
	P1IFG &= ~BIT3; // P1.3 interrupt flag cleared
}
