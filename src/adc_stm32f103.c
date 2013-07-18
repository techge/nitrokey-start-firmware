/*
 * adc_stm32f103.c - ADC driver for STM32F103
 *
 * Copyright (C) 2011, 2012, 2013 Free Software Initiative of Japan
 * Author: NIIBE Yutaka <gniibe@fsij.org>
 *
 * This file is a part of NeuG, a True Random Number Generator
 * implementation based on quantization error of ADC (for STM32F103).
 *
 * NeuG is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NeuG is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <chopstx.h>

#include "neug.h"
#include "stm32f103.h"
#include "adc.h"

#define NEUG_CRC32_COUNTS 4

#define STM32_ADC_ADC1_DMA_PRIORITY         2

#define ADC_SMPR1_SMP_VREF(n)   ((n) << 21)
#define ADC_SMPR1_SMP_SENSOR(n) ((n) << 18)

#define ADC_SMPR1_SMP_AN10(n)   ((n) << 0)
#define ADC_SMPR1_SMP_AN11(n)   ((n) << 3)

#define ADC_SMPR2_SMP_AN0(n)    ((n) << 0)
#define ADC_SMPR2_SMP_AN1(n)    ((n) << 3)
#define ADC_SMPR2_SMP_AN2(n)    ((n) << 6)
#define ADC_SMPR2_SMP_AN9(n)    ((n) << 27)

#define ADC_SQR1_NUM_CH(n)      (((n) - 1) << 20)

#define ADC_SQR3_SQ1_N(n)       ((n) << 0)
#define ADC_SQR3_SQ2_N(n)       ((n) << 5)
#define ADC_SQR3_SQ3_N(n)       ((n) << 10)
#define ADC_SQR3_SQ4_N(n)       ((n) << 15)

#define ADC_SAMPLE_1P5          0

#define ADC_CHANNEL_IN0         0
#define ADC_CHANNEL_IN1         1
#define ADC_CHANNEL_IN2         2
#define ADC_CHANNEL_IN9         9
#define ADC_CHANNEL_IN10        10
#define ADC_CHANNEL_IN11        11
#define ADC_CHANNEL_SENSOR      16
#define ADC_CHANNEL_VREFINT     17

#define DELIBARATELY_DO_IT_WRONG_VREF_SAMPLE_TIME
#define DELIBARATELY_DO_IT_WRONG_START_STOP

#ifdef DELIBARATELY_DO_IT_WRONG_VREF_SAMPLE_TIME
#define ADC_SAMPLE_VREF ADC_SAMPLE_1P5
#define ADC_SAMPLE_SENSOR ADC_SAMPLE_1P5
#else
#define ADC_SAMPLE_VREF ADC_SAMPLE_239P5
#define ADC_SAMPLE_SENSOR ADC_SAMPLE_239P5
#endif

#define NEUG_DMA_CHANNEL STM32_DMA1_STREAM1
#define NEUG_DMA_MODE_SAMPLE                                            \
  (  STM32_DMA_CR_PL (STM32_ADC_ADC1_DMA_PRIORITY)			\
     | STM32_DMA_CR_MSIZE_WORD | STM32_DMA_CR_PSIZE_WORD		\
     | STM32_DMA_CR_MINC       | STM32_DMA_CR_TCIE			\
     | STM32_DMA_CR_TEIE)

#define NEUG_DMA_MODE_CRC32                                             \
  (  STM32_DMA_CR_PL (STM32_ADC_ADC1_DMA_PRIORITY)			\
     | STM32_DMA_CR_MSIZE_WORD | STM32_DMA_CR_PSIZE_WORD		\
     | STM32_DMA_CR_MINC       						\
     | STM32_DMA_CR_TCIE       | STM32_DMA_CR_TEIE)

#define NEUG_ADC_SETTING1_SMPR1 ADC_SMPR1_SMP_VREF(ADC_SAMPLE_VREF)     \
                              | ADC_SMPR1_SMP_SENSOR(ADC_SAMPLE_SENSOR)
#define NEUG_ADC_SETTING1_SMPR2 0
#define NEUG_ADC_SETTING1_SQR3  ADC_SQR3_SQ1_N(ADC_CHANNEL_VREFINT)     \
                              | ADC_SQR3_SQ2_N(ADC_CHANNEL_SENSOR)      \
                              | ADC_SQR3_SQ3_N(ADC_CHANNEL_SENSOR)      \
                              | ADC_SQR3_SQ4_N(ADC_CHANNEL_VREFINT)     
#define NEUG_ADC_SETTING1_NUM_CHANNELS 4

#if !defined(NEUG_ADC_SETTING2_SMPR1)
#define NEUG_ADC_SETTING2_SMPR1 0
#define NEUG_ADC_SETTING2_SMPR2 ADC_SMPR2_SMP_AN0(ADC_SAMPLE_1P5)    \
                              | ADC_SMPR2_SMP_AN1(ADC_SAMPLE_1P5)
#define NEUG_ADC_SETTING2_SQR3  ADC_SQR3_SQ1_N(ADC_CHANNEL_IN0)      \
                              | ADC_SQR3_SQ2_N(ADC_CHANNEL_IN1)
#define NEUG_ADC_SETTING2_NUM_CHANNELS 2
#endif


/*
 * Do calibration for both of ADCs.
 */
void adc_init (void)
{
  RCC->APB2ENR |= (RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN);
  RCC->APB2RSTR = (RCC_APB2RSTR_ADC1RST | RCC_APB2RSTR_ADC2RST);
  RCC->APB2RSTR = 0;

  ADC1->CR1 = 0;
  ADC1->CR2 = ADC_CR2_ADON;
  ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_RSTCAL;
  while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0)
    ;
  ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_CAL;
  while ((ADC1->CR2 & ADC_CR2_CAL) != 0)
    ;
  ADC1->CR2 = 0;

  ADC2->CR1 = 0;
  ADC2->CR2 = ADC_CR2_ADON;
  ADC2->CR2 = ADC_CR2_ADON | ADC_CR2_RSTCAL;
  while ((ADC2->CR2 & ADC_CR2_RSTCAL) != 0)
    ;
  ADC2->CR2 = ADC_CR2_ADON | ADC_CR2_CAL;
  while ((ADC2->CR2 & ADC_CR2_CAL) != 0)
    ;
  ADC2->CR2 = 0;
  RCC->APB2ENR &= ~(RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN);
}

extern uint8_t __process4_stack_base__, __process4_stack_size__;
const uint32_t __stackaddr_adc = (uint32_t)&__process4_stack_base__;
const size_t __stacksize_adc = (size_t)&__process4_stack_size__;
#define PRIO_ADC 3

static void adc_lld_serve_rx_interrupt (uint32_t flags);

#define INTR_REQ_DMA1_Channel1 11
static void *
adc_intr_thread (void *arg)
{
  chopstx_intr_t interrupt;

  (void)arg;
  chopstx_claim_irq (&interrupt, INTR_REQ_DMA1_Channel1);

  while (1)
    {
      uint32_t flags;

      chopstx_intr_wait (&interrupt);
      flags = DMA1->ISR & STM32_DMA_ISR_MASK; /* Channel 1 interrupt cause.  */
      DMA1->IFCR = STM32_DMA_ISR_MASK; /* Clear interrupt of channel 1.  */
      adc_lld_serve_rx_interrupt (flags);
    }

  return NULL;
}

static chopstx_t adc_thd;

void adc_start (void)
{
  /* Use DMA channel 1.  */
  RCC->AHBENR |= RCC_AHBENR_DMA1EN;
  DMA1_Channel1->CCR = STM32_DMA_CCR_RESET_VALUE;
  DMA1->IFCR = 0xffffffff;

  RCC->APB2ENR |= (RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN);

  ADC1->CR1 = (ADC_CR1_DUALMOD_2 | ADC_CR1_DUALMOD_1 | ADC_CR1_DUALMOD_0
	       | ADC_CR1_SCAN);
  ADC1->CR2 = (ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_SWSTART
	       | ADC_CR2_EXTSEL | ADC_CR2_DMA | ADC_CR2_CONT | ADC_CR2_ADON);
  ADC1->SMPR1 = NEUG_ADC_SETTING1_SMPR1;
  ADC1->SMPR2 = NEUG_ADC_SETTING1_SMPR2;
  ADC1->SQR1 = ADC_SQR1_NUM_CH(NEUG_ADC_SETTING1_NUM_CHANNELS);
  ADC1->SQR2 = 0;
  ADC1->SQR3 = NEUG_ADC_SETTING1_SQR3;

  ADC2->CR1 = (ADC_CR1_DUALMOD_2 | ADC_CR1_DUALMOD_1 | ADC_CR1_DUALMOD_0
	       | ADC_CR1_SCAN);
  ADC2->CR2 = ADC_CR2_EXTTRIG | ADC_CR2_CONT | ADC_CR2_ADON;
  ADC2->SMPR1 = NEUG_ADC_SETTING2_SMPR1;
  ADC2->SMPR2 = NEUG_ADC_SETTING2_SMPR2;
  ADC2->SQR1 = ADC_SQR1_NUM_CH(NEUG_ADC_SETTING2_NUM_CHANNELS);
  ADC2->SQR2 = 0;
  ADC2->SQR3 = NEUG_ADC_SETTING2_SQR3;

#ifdef DELIBARATELY_DO_IT_WRONG_START_STOP
  ADC2->CR2 = 0;
  ADC1->CR2 = 0;
#endif

  adc_thd = chopstx_create (PRIO_ADC, __stackaddr_adc, __stacksize_adc,
			    adc_intr_thread, NULL);
}

static int adc_mode;
static uint32_t *adc_ptr;
static int adc_size;
static uint32_t adc_buf[64];

static void adc_start_conversion_internal (void)
{
#ifdef DELIBARATELY_DO_IT_WRONG_START_STOP
  /* Power on */
  ADC2->CR2 = ADC_CR2_EXTTRIG | ADC_CR2_CONT | ADC_CR2_ADON;
  ADC1->CR2 = (ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_SWSTART
	       | ADC_CR2_EXTSEL | ADC_CR2_DMA | ADC_CR2_CONT | ADC_CR2_ADON);
  /* Start conversion.  tSTAB is 1uS, but we don't follow the spec.  */
  ADC2->CR2 = ADC_CR2_EXTTRIG | ADC_CR2_CONT | ADC_CR2_ADON;
  ADC1->CR2 = (ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_SWSTART
	       | ADC_CR2_EXTSEL | ADC_CR2_DMA | ADC_CR2_CONT | ADC_CR2_ADON);
#else
  ADC2->CR2 = ADC_CR2_EXTTRIG | ADC_CR2_CONT | ADC_CR2_ADON;
  ADC1->CR2 = (ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_SWSTART
	       | ADC_CR2_EXTSEL | ADC_CR2_DMA | ADC_CR2_CONT | ADC_CR2_ADON);
#endif
}

void adc_start_conversion (int mode, uint32_t *p, int size)
{
  adc_mode = mode;
  adc_ptr = p;
  adc_size = size;

 if (mode == ADC_SAMPLE_MODE)
    {
      DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR; /* SetPeripheral */
      DMA1_Channel1->CMAR  = (uint32_t)p; /* SetMemory0 */
      DMA1_Channel1->CNDTR  = (uint32_t)size / 4; /* counter */
      DMA1_Channel1->CCR  = NEUG_DMA_MODE_SAMPLE; /*mode*/
      DMA1_Channel1->CCR |= DMA_CCR1_EN;		    /* Enable */
    }
  else
    {
      DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR; /* SetPeripheral */
      DMA1_Channel1->CMAR  = (uint32_t)adc_buf; /* SetMemory0 */
      DMA1_Channel1->CNDTR  = size; /* counter */
      DMA1_Channel1->CCR  = NEUG_DMA_MODE_CRC32; /*mode*/
      DMA1_Channel1->CCR |= DMA_CCR1_EN;		    /* Enable */
    }

 adc_start_conversion_internal ();
}


static void adc_stop_conversion (void)
{
  DMA1_Channel1->CCR &= ~DMA_CCR1_EN;

#ifdef DELIBARATELY_DO_IT_WRONG_START_STOP
  ADC1->CR2 = 0;
  ADC2->CR2 = 0;
#else
  ADC2->CR2 &= ~ADC_CR2_CONT;
  ADC1->CR2 &= ~ADC_CR2_CONT;
#endif
}

void adc_stop (void)
{
  ADC1->CR1 = 0;
  ADC1->CR2 = 0;

  ADC2->CR1 = 0;
  ADC2->CR2 = 0;

  RCC->AHBENR &= ~RCC_AHBENR_DMA1EN;
  RCC->APB2ENR &= ~(RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN);

  chopstx_cancel (adc_thd);
  chopstx_join (adc_thd, NULL);
}


static void adc_lld_serve_rx_interrupt (uint32_t flags)
{
  if ((flags & STM32_DMA_ISR_TEIF) != 0)  /* DMA errors  */
    {
      /* Should never happened.  If any, it's coding error. */
      /* Access an unmapped address space or alignment violation.  */
      adc_stop_conversion ();
    }
  else
    {
      if ((flags & STM32_DMA_ISR_TCIF) != 0) /* Transfer complete */
	{
	  adc_stop_conversion ();

	  if (adc_mode != ADC_SAMPLE_MODE)
	    {
	      int i;

	      for (i = 0; i < adc_size;)
		{
		  CRC->DR = adc_buf[i++];
		  CRC->DR = adc_buf[i++];
		  CRC->DR = adc_buf[i++];
		  CRC->DR = adc_buf[i++];
		  *adc_ptr++ = CRC->DR;
		}
	    }

	  chopstx_mutex_lock (&adc_mtx);
	  adc_data_available++;
	  if (adc_waiting)
	    chopstx_cond_signal (&adc_cond);
	  chopstx_mutex_unlock (&adc_mtx);
	}
    }
}