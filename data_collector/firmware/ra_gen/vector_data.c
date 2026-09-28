/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = iic_master_rxi_isr, /* IIC1 RXI (Receive data full) */
            [1] = iic_master_txi_isr, /* IIC1 TXI (Transmit data empty) */
            [2] = iic_master_tei_isr, /* IIC1 TEI (Transmit end) */
            [3] = iic_master_eri_isr, /* IIC1 ERI (Transfer error) */
            [4] = sci_b_uart_rxi_isr, /* SCI8 RXI (Receive data full) */
            [5] = sci_b_uart_txi_isr, /* SCI8 TXI (Transmit data empty) */
            [6] = sci_b_uart_tei_isr, /* SCI8 TEI (Transmit end) */
            [7] = sci_b_uart_eri_isr, /* SCI8 ERI (Receive error) */
            [8] = adc_b_limclpi_isr, /* ADC LIMCLPI (Limiter clip interrupt with the limit table 0 to 7) */
            [9] = adc_b_err0_isr, /* ADC ERR0 (A/D converter unit 0 Error) */
            [10] = adc_b_err1_isr, /* ADC ERR1 (A/D converter unit 1 Error) */
            [11] = adc_b_resovf0_isr, /* ADC RESOVF0 (A/D conversion overflow on A/D converter unit 0) */
            [12] = adc_b_resovf1_isr, /* ADC RESOVF1 (A/D conversion overflow on A/D converter unit 1) */
            [13] = adc_b_calend0_isr, /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
            [14] = adc_b_calend1_isr, /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
            [15] = adc_b_adi0_isr, /* ADC ADI0 (End of A/D scanning operation(Gr.0)) */
            [16] = adc_b_adi1_isr, /* ADC ADI1 (End of A/D scanning operation(Gr.1)) */
            [17] = adc_b_adi2_isr, /* ADC ADI2 (End of A/D scanning operation(Gr.2)) */
            [18] = adc_b_adi3_isr, /* ADC ADI3 (End of A/D scanning operation(Gr.3)) */
            [19] = adc_b_adi4_isr, /* ADC ADI4 (End of A/D scanning operation(Gr.4)) */
            [20] = adc_b_fifoovf_isr, /* ADC FIFOOVF (FIFO data overflow) */
            [21] = adc_b_fiforeq0_isr, /* ADC FIFOREQ0 (FIFO data read request interrupt(Gr.0)) */
            [22] = adc_b_fiforeq1_isr, /* ADC FIFOREQ1 (FIFO data read request interrupt(Gr.1)) */
            [23] = adc_b_fiforeq2_isr, /* ADC FIFOREQ2 (FIFO data read request interrupt(Gr.2)) */
            [24] = adc_b_fiforeq3_isr, /* ADC FIFOREQ3 (FIFO data read request interrupt(Gr.3)) */
            [25] = adc_b_fiforeq4_isr, /* ADC FIFOREQ4 (FIFO data read request interrupt(Gr.4)) */
            [26] = r_icu_isr, /* ICU IRQ13 (External pin interrupt 13) */
            [27] = r_icu_isr, /* ICU IRQ12 (External pin interrupt 12) */
            [28] = gpt_counter_overflow_isr, /* GPT1 COUNTER OVERFLOW (Overflow) */
            [29] = r_icu_isr, /* ICU IRQ19 (External pin interrupt 19) */
            [30] = gpt_counter_overflow_isr, /* GPT5 COUNTER OVERFLOW (Overflow) */
            [31] = gpt_counter_overflow_isr, /* GPT3 COUNTER OVERFLOW (Overflow) */
            [32] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
            [33] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
            [34] = usbfs_d0fifo_handler, /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [35] = usbfs_d1fifo_handler, /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [36] = usbhs_interrupt_handler, /* USBHS USB INT RESUME (USBHS interrupt) */
            [37] = usbhs_d0fifo_handler, /* USBHS FIFO 0 (DMA transfer request 0) */
            [38] = usbhs_d1fifo_handler, /* USBHS FIFO 1 (DMA transfer request 1) */
            [39] = glcdc_line_detect_isr, /* GLCDC LINE DETECT (Specified line) */
            [40] = glcdc_underflow_1_isr, /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
            [41] = glcdc_underflow_2_isr, /* GLCDC UNDERFLOW 2 (Graphic 2 underflow) */
            [42] = drw_int_isr, /* DRW INT (DRW interrupt) */
            [43] = vin_status_isr, /* VIN IRQ (Interrupt Request) */
            [44] = vin_error_isr, /* VIN ERR (Interrupt Request for SYNC Error) */
            [45] = mipi_csi_rx_isr, /* MIPICSI RX (Receive interrupt) */
            [46] = mipi_csi_dl_isr, /* MIPICSI DL (Data Lane interrupt) */
            [47] = mipi_csi_vc_isr, /* MIPICSI VC (Virtual Channel interrupt) */
            [48] = mipi_csi_pm_isr, /* MIPICSI PM (Power Management interrupt) */
            [49] = mipi_csi_gst_isr, /* MIPICSI GST (Generic Short Packet interrupt) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_IIC1_RXI,GROUP0), /* IIC1 RXI (Receive data full) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TXI,GROUP1), /* IIC1 TXI (Transmit data empty) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TEI,GROUP2), /* IIC1 TEI (Transmit end) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_IIC1_ERI,GROUP3), /* IIC1 ERI (Transfer error) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_SCI8_RXI,GROUP4), /* SCI8 RXI (Receive data full) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TXI,GROUP5), /* SCI8 TXI (Transmit data empty) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TEI,GROUP6), /* SCI8 TEI (Transmit end) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_SCI8_ERI,GROUP7), /* SCI8 ERI (Receive error) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_ADC_LIMCLPI,GROUP0), /* ADC LIMCLPI (Limiter clip interrupt with the limit table 0 to 7) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_ADC_ERR0,GROUP1), /* ADC ERR0 (A/D converter unit 0 Error) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_ADC_ERR1,GROUP2), /* ADC ERR1 (A/D converter unit 1 Error) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_ADC_RESOVF0,GROUP3), /* ADC RESOVF0 (A/D conversion overflow on A/D converter unit 0) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_ADC_RESOVF1,GROUP4), /* ADC RESOVF1 (A/D conversion overflow on A/D converter unit 1) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_ADC_CALEND0,GROUP5), /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
            [14] = BSP_PRV_VECT_ENUM(EVENT_ADC_CALEND1,GROUP6), /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
            [15] = BSP_PRV_VECT_ENUM(EVENT_ADC_ADI0,GROUP7), /* ADC ADI0 (End of A/D scanning operation(Gr.0)) */
            [16] = BSP_PRV_VECT_ENUM(EVENT_ADC_ADI1,GROUP0), /* ADC ADI1 (End of A/D scanning operation(Gr.1)) */
            [17] = BSP_PRV_VECT_ENUM(EVENT_ADC_ADI2,GROUP1), /* ADC ADI2 (End of A/D scanning operation(Gr.2)) */
            [18] = BSP_PRV_VECT_ENUM(EVENT_ADC_ADI3,GROUP2), /* ADC ADI3 (End of A/D scanning operation(Gr.3)) */
            [19] = BSP_PRV_VECT_ENUM(EVENT_ADC_ADI4,GROUP3), /* ADC ADI4 (End of A/D scanning operation(Gr.4)) */
            [20] = BSP_PRV_VECT_ENUM(EVENT_ADC_FIFOOVF,GROUP4), /* ADC FIFOOVF (FIFO data overflow) */
            [21] = BSP_PRV_VECT_ENUM(EVENT_ADC_FIFOREQ0,GROUP5), /* ADC FIFOREQ0 (FIFO data read request interrupt(Gr.0)) */
            [22] = BSP_PRV_VECT_ENUM(EVENT_ADC_FIFOREQ1,GROUP6), /* ADC FIFOREQ1 (FIFO data read request interrupt(Gr.1)) */
            [23] = BSP_PRV_VECT_ENUM(EVENT_ADC_FIFOREQ2,GROUP7), /* ADC FIFOREQ2 (FIFO data read request interrupt(Gr.2)) */
            [24] = BSP_PRV_VECT_ENUM(EVENT_ADC_FIFOREQ3,GROUP0), /* ADC FIFOREQ3 (FIFO data read request interrupt(Gr.3)) */
            [25] = BSP_PRV_VECT_ENUM(EVENT_ADC_FIFOREQ4,GROUP1), /* ADC FIFOREQ4 (FIFO data read request interrupt(Gr.4)) */
            [26] = BSP_PRV_VECT_ENUM(EVENT_ICU_IRQ13,GROUP2), /* ICU IRQ13 (External pin interrupt 13) */
            [27] = BSP_PRV_VECT_ENUM(EVENT_ICU_IRQ12,GROUP3), /* ICU IRQ12 (External pin interrupt 12) */
            [28] = BSP_PRV_VECT_ENUM(EVENT_GPT1_COUNTER_OVERFLOW,GROUP4), /* GPT1 COUNTER OVERFLOW (Overflow) */
            [29] = BSP_PRV_VECT_ENUM(EVENT_ICU_IRQ19,GROUP5), /* ICU IRQ19 (External pin interrupt 19) */
            [30] = BSP_PRV_VECT_ENUM(EVENT_GPT5_COUNTER_OVERFLOW,GROUP6), /* GPT5 COUNTER OVERFLOW (Overflow) */
            [31] = BSP_PRV_VECT_ENUM(EVENT_GPT3_COUNTER_OVERFLOW,GROUP7), /* GPT3 COUNTER OVERFLOW (Overflow) */
            [32] = BSP_PRV_VECT_ENUM(EVENT_USBFS_INT,FIXED), /* USBFS INT (USBFS interrupt) */
            [33] = BSP_PRV_VECT_ENUM(EVENT_USBFS_RESUME,FIXED), /* USBFS RESUME (USBFS resume interrupt) */
            [34] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_0,FIXED), /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [35] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_1,FIXED), /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [36] = BSP_PRV_VECT_ENUM(EVENT_USBHS_USB_INT_RESUME,FIXED), /* USBHS USB INT RESUME (USBHS interrupt) */
            [37] = BSP_PRV_VECT_ENUM(EVENT_USBHS_FIFO_0,FIXED), /* USBHS FIFO 0 (DMA transfer request 0) */
            [38] = BSP_PRV_VECT_ENUM(EVENT_USBHS_FIFO_1,FIXED), /* USBHS FIFO 1 (DMA transfer request 1) */
            [39] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_LINE_DETECT,FIXED), /* GLCDC LINE DETECT (Specified line) */
            [40] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_UNDERFLOW_1,FIXED), /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
            [41] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_UNDERFLOW_2,FIXED), /* GLCDC UNDERFLOW 2 (Graphic 2 underflow) */
            [42] = BSP_PRV_VECT_ENUM(EVENT_DRW_INT,FIXED), /* DRW INT (DRW interrupt) */
            [43] = BSP_PRV_VECT_ENUM(EVENT_VIN_IRQ,FIXED), /* VIN IRQ (Interrupt Request) */
            [44] = BSP_PRV_VECT_ENUM(EVENT_VIN_ERR,FIXED), /* VIN ERR (Interrupt Request for SYNC Error) */
            [45] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_RX,FIXED), /* MIPICSI RX (Receive interrupt) */
            [46] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_DL,FIXED), /* MIPICSI DL (Data Lane interrupt) */
            [47] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_VC,FIXED), /* MIPICSI VC (Virtual Channel interrupt) */
            [48] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_PM,FIXED), /* MIPICSI PM (Power Management interrupt) */
            [49] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_GST,FIXED), /* MIPICSI GST (Generic Short Packet interrupt) */
        };
        #endif
        #endif