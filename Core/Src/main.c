/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
LPTIM_HandleTypeDef hlptim1;

/* USER CODE BEGIN PV */
// If you want to use debugging, enable USART in Asynchronous mode
// DEbug info will appear on the STM COM port (use teraterm)
#undef MY_DEBUG

#define LED_ON
#define STARTUP_DELAY 15000

#define nADCP 2
#define nWindows 10

#undef HAS_ECHO // if we plan to use ECHOSOUNDER

// Ping schedule for the 2 ADCPs (remember to leave space for \0 terminator)
// Caveat: Currently, we don't optimize for synchronous pinging (i.e., both ADCPs having an "X" in the same window)
// 	We could detect this and use "TriggerAllADCPs" function instead of triggering them individually.
// 	This is not done, though.
#ifndef HAS_ECHO
// three-ping ensembles
static const char Pings[nADCP][nWindows+1] = {
			//   0123456789
				"XXXooooooo",
				"oooooXXXoo"
	};
#else
// four-ping ensembles
static const char Pings[nADCP][nWindows+1] = {
			//   0123456789
				"XXXXoooooo",
				"oooooXXXXo"
	};

#endif

// The idea is to start with several synchronous pings
#define NSTARTUP 6 // number of startup pings. might want to be multiple of ensemble size



// For 16 windows, LPTIM_PERIOD = 2048, so the update rate is 32768 / 2048 = 16 Hz
// For 10 windows, LPTIM_PERIOD = 3277, so the update rate is 32768 / 3277 =  9.99938 Hz (slightly shorter)
#define LSE_HZ 32768U
#define LPTIM_PERIOD ((LSE_HZ + nWindows/2) / nWindows)

// To describe the sync lines for each ADCP, we store the BSRR values for two states
// We compute these BSRR states by specifying the two pins
// We do not need to keep the pin numbers!
typedef struct {
    // pre-computed BSRR values
    uint32_t bsrr_state0;   // A=1, B=0
    uint32_t bsrr_state1;   // A=0, B=1
} ADCP_LinePair;

// helper macro that pre-computes BSRR
#define ADCP_PAIR(a_, b_) \
    { \
        .bsrr_state0 = ((uint32_t)(b_) << 16) | (a_), \
        .bsrr_state1 = ((uint32_t)(a_) << 16) | (b_) \
    }

// ADCP sync_out pins are *all on GPIOB*
#define ADCP_GPIO_PORT GPIOB
//
// For each ADCP, we specify two pins (A & B) ...
// Nucleo pin 	STM pin
//		D3			PB0
//		D4			PB7
//		D5			PB6
//		D6			PB1

#define D3 GPIO_PIN_0
#define D4 GPIO_PIN_7
#define D5 GPIO_PIN_6
#define D6 GPIO_PIN_1

// As of May 15. 2025
//const int pinADCP_RS485[nADCP][2] = {{D6, D4},
//									 {D5, D3}};

#undef RIOT_MISWIRE
#ifndef RIOT_MISWIRE
// Correct wiring
static const ADCP_LinePair ADCP[nADCP] = {
		ADCP_PAIR(D6, D4),   // ADCP 0
		ADCP_PAIR(D5, D3)    // ADCP 1
};
#else
// Incorrect (RIOT)  wiring
static const ADCP_LinePair ADCP[nADCP] = {
		ADCP_PAIR(D6, D5),   // ADCP 0
		ADCP_PAIR(D4, D3)    // ADCP 1
};
#endif

// Define ADCP line pair state
// 	state = 0 => A=1, B=0
// 	state = 1 => A=0, B=1
// Could be the other way around, the states themselves don't matter
// The state *change* triggers the ADCP
static uint8_t ADCP_State[nADCP] = {0, 0};



/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPTIM1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void ADCP_Pin_Init(void) {
	// synchronous initialization
    uint32_t bsrr = 0;
    uint8_t a;

    for (a = 0; a < nADCP; a++) {
    	 ADCP_State[a] = 0;              // start in state 0: A=1, B=0
    	 bsrr |= ADCP[a].bsrr_state0;    // collect all outputs into one GPIOB write
    }

    ADCP_GPIO_PORT->BSRR = bsrr;
}

static void TriggerADCP(uint8_t a) {
	// trigger a given ADCP
    if (a < 0 || a >= nADCP) return;

    ADCP_State[a] ^= 1u;   // swap polarity
    ADCP_GPIO_PORT->BSRR = ADCP_State[a] ? ADCP[a].bsrr_state1
    									 : ADCP[a].bsrr_state0;
}

static void TriggerAllADCPs(void){
	// synchronous trigger (used for clock sync pulses)
    uint32_t bsrr = 0;
    uint8_t a;

        for (a = 0; a < nADCP; a++) {
            ADCP_State[a] ^= 1u;
            bsrr |= ADCP_State[a] ? ADCP[a].bsrr_state1
                                  : ADCP[a].bsrr_state0;
        }

        ADCP_GPIO_PORT->BSRR = bsrr;
}

static void ADCP_Sync_Window(void){
	// This function is called on regular intervals
	// We cannot check the timing, just rely on LPTIM
	static uint8_t StartupRemaining = NSTARTUP; // remaining startup pings
	static uint8_t Window = 0; // current window
	uint8_t a;
	uint8_t Active;


	Active = 0;
#ifdef MY_DEBUG
	HAL_UART_DeInit(&huart2);
	HAL_UART_Init(&huart2);
	printf("Window %d...\r\n",Window);
#endif

	if (StartupRemaining){

		// startup pings, trigger both ADCPs as fast as possible
		Active = 1;
		TriggerAllADCPs();
		StartupRemaining--;
	}
	else {
		// regular pinging
		for(a=0;a<nADCP;a++){
			if (Pings[a][Window] == 'X') {
				// This window is active, toggle the line(s)
				Active = 1;
				TriggerADCP(a);

#ifdef MY_DEBUG
				printf("%d",a);
#endif
			}
		}
		// Move to the next window
		Window = (Window + 1)%nWindows;
	}


	#ifdef LED_ON
		if (Active){
			// If this was an active window for any of the ADCPs, turn the LED on
			HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
		}
		else{
			// Turn LED OFF
			HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
		}
	#endif
}

#ifdef MY_DEBUG
	int _write(int file, char *ptr, int len)
	{
		HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
		return len;
	}
#endif

void HAL_LPTIM_AutoReloadMatchCallback(LPTIM_HandleTypeDef *hlptim)	{
	if (hlptim->Instance == LPTIM1) 	{
		ADCP_Sync_Window();
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_LPTIM1_Init();
  /* USER CODE BEGIN 2 */
  ADCP_Pin_Init();
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET); // LED on
  HAL_Delay(STARTUP_DELAY);  // Wait some time to allow debugger to connect (or allow both ADCPs to initialize)
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin,  GPIO_PIN_RESET); // LED off

 // Set the timer counter using pre-computed period (need to subtract 1)
  // HAL_LPTIM_Counter_Start_IT(&hlptim1, round(32768.0/nWindows)-1 ); // this requires <math.h>
  HAL_LPTIM_Counter_Start_IT(&hlptim1, LPTIM_PERIOD - 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	 HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV64;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPTIM1;
  PeriphClkInit.LptimClockSelection = RCC_LPTIM1CLKSOURCE_LSE;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPTIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPTIM1_Init(void)
{

  /* USER CODE BEGIN LPTIM1_Init 0 */

  /* USER CODE END LPTIM1_Init 0 */

  /* USER CODE BEGIN LPTIM1_Init 1 */

  /* USER CODE END LPTIM1_Init 1 */
  hlptim1.Instance = LPTIM1;
  hlptim1.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
  hlptim1.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV1;
  hlptim1.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
  hlptim1.Init.OutputPolarity = LPTIM_OUTPUTPOLARITY_HIGH;
  hlptim1.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
  hlptim1.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
  if (HAL_LPTIM_Init(&hlptim1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPTIM1_Init 2 */

  /* USER CODE END LPTIM1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, A1_A_Pin|A2_B_Pin|LD3_Pin|A1_B_Pin
                          |A2_A_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA1 PA3 PA4
                           PA5 PA6 PA7 PA8
                           PA9 PA10 PA11 PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_4
                          |GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : VCP_TX_Pin VCP_RX_Pin */
  GPIO_InitStruct.Pin = VCP_TX_Pin|VCP_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : A1_A_Pin A2_B_Pin A1_B_Pin A2_A_Pin */
  GPIO_InitStruct.Pin = A1_A_Pin|A2_B_Pin|A1_B_Pin|A2_A_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
