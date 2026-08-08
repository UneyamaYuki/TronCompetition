/* generated vector header file - do not edit */
        #ifndef VECTOR_DATA_H
        #define VECTOR_DATA_H
        #ifdef __cplusplus
        extern "C" {
        #endif
                /* Number of interrupts allocated */
        #ifndef VECTOR_DATA_IRQ_COUNT
        #define VECTOR_DATA_IRQ_COUNT    (43)
        #endif
        /* ISR prototypes */
        void iic_master_rxi_isr(void);
        void iic_master_txi_isr(void);
        void iic_master_tei_isr(void);
        void iic_master_eri_isr(void);
        void sci_b_uart_rxi_isr(void);
        void sci_b_uart_txi_isr(void);
        void sci_b_uart_tei_isr(void);
        void sci_b_uart_eri_isr(void);
        void adc_b_limclpi_isr(void);
        void adc_b_err0_isr(void);
        void adc_b_err1_isr(void);
        void adc_b_resovf0_isr(void);
        void adc_b_resovf1_isr(void);
        void adc_b_calend0_isr(void);
        void adc_b_calend1_isr(void);
        void adc_b_adi0_isr(void);
        void adc_b_adi1_isr(void);
        void adc_b_adi2_isr(void);
        void adc_b_adi3_isr(void);
        void adc_b_adi4_isr(void);
        void adc_b_fifoovf_isr(void);
        void adc_b_fiforeq0_isr(void);
        void adc_b_fiforeq1_isr(void);
        void adc_b_fiforeq2_isr(void);
        void adc_b_fiforeq3_isr(void);
        void adc_b_fiforeq4_isr(void);
        void r_icu_isr(void);
        void gpt_counter_overflow_isr(void);
        void glcdc_line_detect_isr(void);
        void glcdc_underflow_1_isr(void);
        void glcdc_underflow_2_isr(void);
        void drw_int_isr(void);
        void vin_status_isr(void);
        void vin_error_isr(void);
        void mipi_csi_rx_isr(void);
        void mipi_csi_dl_isr(void);
        void mipi_csi_vc_isr(void);
        void mipi_csi_pm_isr(void);
        void mipi_csi_gst_isr(void);

        /* Vector table allocations */
        #define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type) 0) /* IIC1 RXI (Receive data full) */
        #define IIC1_RXI_IRQn          ((IRQn_Type) 0) /* IIC1 RXI (Receive data full) */
        #define VECTOR_NUMBER_IIC1_TXI ((IRQn_Type) 1) /* IIC1 TXI (Transmit data empty) */
        #define IIC1_TXI_IRQn          ((IRQn_Type) 1) /* IIC1 TXI (Transmit data empty) */
        #define VECTOR_NUMBER_IIC1_TEI ((IRQn_Type) 2) /* IIC1 TEI (Transmit end) */
        #define IIC1_TEI_IRQn          ((IRQn_Type) 2) /* IIC1 TEI (Transmit end) */
        #define VECTOR_NUMBER_IIC1_ERI ((IRQn_Type) 3) /* IIC1 ERI (Transfer error) */
        #define IIC1_ERI_IRQn          ((IRQn_Type) 3) /* IIC1 ERI (Transfer error) */
        #define VECTOR_NUMBER_SCI8_RXI ((IRQn_Type) 4) /* SCI8 RXI (Receive data full) */
        #define SCI8_RXI_IRQn          ((IRQn_Type) 4) /* SCI8 RXI (Receive data full) */
        #define VECTOR_NUMBER_SCI8_TXI ((IRQn_Type) 5) /* SCI8 TXI (Transmit data empty) */
        #define SCI8_TXI_IRQn          ((IRQn_Type) 5) /* SCI8 TXI (Transmit data empty) */
        #define VECTOR_NUMBER_SCI8_TEI ((IRQn_Type) 6) /* SCI8 TEI (Transmit end) */
        #define SCI8_TEI_IRQn          ((IRQn_Type) 6) /* SCI8 TEI (Transmit end) */
        #define VECTOR_NUMBER_SCI8_ERI ((IRQn_Type) 7) /* SCI8 ERI (Receive error) */
        #define SCI8_ERI_IRQn          ((IRQn_Type) 7) /* SCI8 ERI (Receive error) */
        #define VECTOR_NUMBER_ADC_LIMCLPI ((IRQn_Type) 8) /* ADC LIMCLPI (Limiter clip interrupt with the limit table 0 to 7) */
        #define ADC_LIMCLPI_IRQn          ((IRQn_Type) 8) /* ADC LIMCLPI (Limiter clip interrupt with the limit table 0 to 7) */
        #define VECTOR_NUMBER_ADC_ERR0 ((IRQn_Type) 9) /* ADC ERR0 (A/D converter unit 0 Error) */
        #define ADC_ERR0_IRQn          ((IRQn_Type) 9) /* ADC ERR0 (A/D converter unit 0 Error) */
        #define VECTOR_NUMBER_ADC_ERR1 ((IRQn_Type) 10) /* ADC ERR1 (A/D converter unit 1 Error) */
        #define ADC_ERR1_IRQn          ((IRQn_Type) 10) /* ADC ERR1 (A/D converter unit 1 Error) */
        #define VECTOR_NUMBER_ADC_RESOVF0 ((IRQn_Type) 11) /* ADC RESOVF0 (A/D conversion overflow on A/D converter unit 0) */
        #define ADC_RESOVF0_IRQn          ((IRQn_Type) 11) /* ADC RESOVF0 (A/D conversion overflow on A/D converter unit 0) */
        #define VECTOR_NUMBER_ADC_RESOVF1 ((IRQn_Type) 12) /* ADC RESOVF1 (A/D conversion overflow on A/D converter unit 1) */
        #define ADC_RESOVF1_IRQn          ((IRQn_Type) 12) /* ADC RESOVF1 (A/D conversion overflow on A/D converter unit 1) */
        #define VECTOR_NUMBER_ADC_CALEND0 ((IRQn_Type) 13) /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
        #define ADC_CALEND0_IRQn          ((IRQn_Type) 13) /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
        #define VECTOR_NUMBER_ADC_CALEND1 ((IRQn_Type) 14) /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
        #define ADC_CALEND1_IRQn          ((IRQn_Type) 14) /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
        #define VECTOR_NUMBER_ADC_ADI0 ((IRQn_Type) 15) /* ADC ADI0 (End of A/D scanning operation(Gr.0)) */
        #define ADC_ADI0_IRQn          ((IRQn_Type) 15) /* ADC ADI0 (End of A/D scanning operation(Gr.0)) */
        #define VECTOR_NUMBER_ADC_ADI1 ((IRQn_Type) 16) /* ADC ADI1 (End of A/D scanning operation(Gr.1)) */
        #define ADC_ADI1_IRQn          ((IRQn_Type) 16) /* ADC ADI1 (End of A/D scanning operation(Gr.1)) */
        #define VECTOR_NUMBER_ADC_ADI2 ((IRQn_Type) 17) /* ADC ADI2 (End of A/D scanning operation(Gr.2)) */
        #define ADC_ADI2_IRQn          ((IRQn_Type) 17) /* ADC ADI2 (End of A/D scanning operation(Gr.2)) */
        #define VECTOR_NUMBER_ADC_ADI3 ((IRQn_Type) 18) /* ADC ADI3 (End of A/D scanning operation(Gr.3)) */
        #define ADC_ADI3_IRQn          ((IRQn_Type) 18) /* ADC ADI3 (End of A/D scanning operation(Gr.3)) */
        #define VECTOR_NUMBER_ADC_ADI4 ((IRQn_Type) 19) /* ADC ADI4 (End of A/D scanning operation(Gr.4)) */
        #define ADC_ADI4_IRQn          ((IRQn_Type) 19) /* ADC ADI4 (End of A/D scanning operation(Gr.4)) */
        #define VECTOR_NUMBER_ADC_FIFOOVF ((IRQn_Type) 20) /* ADC FIFOOVF (FIFO data overflow) */
        #define ADC_FIFOOVF_IRQn          ((IRQn_Type) 20) /* ADC FIFOOVF (FIFO data overflow) */
        #define VECTOR_NUMBER_ADC_FIFOREQ0 ((IRQn_Type) 21) /* ADC FIFOREQ0 (FIFO data read request interrupt(Gr.0)) */
        #define ADC_FIFOREQ0_IRQn          ((IRQn_Type) 21) /* ADC FIFOREQ0 (FIFO data read request interrupt(Gr.0)) */
        #define VECTOR_NUMBER_ADC_FIFOREQ1 ((IRQn_Type) 22) /* ADC FIFOREQ1 (FIFO data read request interrupt(Gr.1)) */
        #define ADC_FIFOREQ1_IRQn          ((IRQn_Type) 22) /* ADC FIFOREQ1 (FIFO data read request interrupt(Gr.1)) */
        #define VECTOR_NUMBER_ADC_FIFOREQ2 ((IRQn_Type) 23) /* ADC FIFOREQ2 (FIFO data read request interrupt(Gr.2)) */
        #define ADC_FIFOREQ2_IRQn          ((IRQn_Type) 23) /* ADC FIFOREQ2 (FIFO data read request interrupt(Gr.2)) */
        #define VECTOR_NUMBER_ADC_FIFOREQ3 ((IRQn_Type) 24) /* ADC FIFOREQ3 (FIFO data read request interrupt(Gr.3)) */
        #define ADC_FIFOREQ3_IRQn          ((IRQn_Type) 24) /* ADC FIFOREQ3 (FIFO data read request interrupt(Gr.3)) */
        #define VECTOR_NUMBER_ADC_FIFOREQ4 ((IRQn_Type) 25) /* ADC FIFOREQ4 (FIFO data read request interrupt(Gr.4)) */
        #define ADC_FIFOREQ4_IRQn          ((IRQn_Type) 25) /* ADC FIFOREQ4 (FIFO data read request interrupt(Gr.4)) */
        #define VECTOR_NUMBER_ICU_IRQ13 ((IRQn_Type) 26) /* ICU IRQ13 (External pin interrupt 13) */
        #define ICU_IRQ13_IRQn          ((IRQn_Type) 26) /* ICU IRQ13 (External pin interrupt 13) */
        #define VECTOR_NUMBER_ICU_IRQ12 ((IRQn_Type) 27) /* ICU IRQ12 (External pin interrupt 12) */
        #define ICU_IRQ12_IRQn          ((IRQn_Type) 27) /* ICU IRQ12 (External pin interrupt 12) */
        #define VECTOR_NUMBER_GPT1_COUNTER_OVERFLOW ((IRQn_Type) 28) /* GPT1 COUNTER OVERFLOW (Overflow) */
        #define GPT1_COUNTER_OVERFLOW_IRQn          ((IRQn_Type) 28) /* GPT1 COUNTER OVERFLOW (Overflow) */
        #define VECTOR_NUMBER_ICU_IRQ19 ((IRQn_Type) 29) /* ICU IRQ19 (External pin interrupt 19) */
        #define ICU_IRQ19_IRQn          ((IRQn_Type) 29) /* ICU IRQ19 (External pin interrupt 19) */
        #define VECTOR_NUMBER_GPT5_COUNTER_OVERFLOW ((IRQn_Type) 30) /* GPT5 COUNTER OVERFLOW (Overflow) */
        #define GPT5_COUNTER_OVERFLOW_IRQn          ((IRQn_Type) 30) /* GPT5 COUNTER OVERFLOW (Overflow) */
        #define VECTOR_NUMBER_GPT3_COUNTER_OVERFLOW ((IRQn_Type) 31) /* GPT3 COUNTER OVERFLOW (Overflow) */
        #define GPT3_COUNTER_OVERFLOW_IRQn          ((IRQn_Type) 31) /* GPT3 COUNTER OVERFLOW (Overflow) */
        #define VECTOR_NUMBER_GLCDC_LINE_DETECT ((IRQn_Type) 32) /* GLCDC LINE DETECT (Specified line) */
        #define GLCDC_LINE_DETECT_IRQn          ((IRQn_Type) 32) /* GLCDC LINE DETECT (Specified line) */
        #define VECTOR_NUMBER_GLCDC_UNDERFLOW_1 ((IRQn_Type) 33) /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
        #define GLCDC_UNDERFLOW_1_IRQn          ((IRQn_Type) 33) /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
        #define VECTOR_NUMBER_GLCDC_UNDERFLOW_2 ((IRQn_Type) 34) /* GLCDC UNDERFLOW 2 (Graphic 2 underflow) */
        #define GLCDC_UNDERFLOW_2_IRQn          ((IRQn_Type) 34) /* GLCDC UNDERFLOW 2 (Graphic 2 underflow) */
        #define VECTOR_NUMBER_DRW_INT ((IRQn_Type) 35) /* DRW INT (DRW interrupt) */
        #define DRW_INT_IRQn          ((IRQn_Type) 35) /* DRW INT (DRW interrupt) */
        #define VECTOR_NUMBER_VIN_IRQ ((IRQn_Type) 36) /* VIN IRQ (Interrupt Request) */
        #define VIN_IRQ_IRQn          ((IRQn_Type) 36) /* VIN IRQ (Interrupt Request) */
        #define VECTOR_NUMBER_VIN_ERR ((IRQn_Type) 37) /* VIN ERR (Interrupt Request for SYNC Error) */
        #define VIN_ERR_IRQn          ((IRQn_Type) 37) /* VIN ERR (Interrupt Request for SYNC Error) */
        #define VECTOR_NUMBER_MIPICSI_RX ((IRQn_Type) 38) /* MIPICSI RX (Receive interrupt) */
        #define MIPICSI_RX_IRQn          ((IRQn_Type) 38) /* MIPICSI RX (Receive interrupt) */
        #define VECTOR_NUMBER_MIPICSI_DL ((IRQn_Type) 39) /* MIPICSI DL (Data Lane interrupt) */
        #define MIPICSI_DL_IRQn          ((IRQn_Type) 39) /* MIPICSI DL (Data Lane interrupt) */
        #define VECTOR_NUMBER_MIPICSI_VC ((IRQn_Type) 40) /* MIPICSI VC (Virtual Channel interrupt) */
        #define MIPICSI_VC_IRQn          ((IRQn_Type) 40) /* MIPICSI VC (Virtual Channel interrupt) */
        #define VECTOR_NUMBER_MIPICSI_PM ((IRQn_Type) 41) /* MIPICSI PM (Power Management interrupt) */
        #define MIPICSI_PM_IRQn          ((IRQn_Type) 41) /* MIPICSI PM (Power Management interrupt) */
        #define VECTOR_NUMBER_MIPICSI_GST ((IRQn_Type) 42) /* MIPICSI GST (Generic Short Packet interrupt) */
        #define MIPICSI_GST_IRQn          ((IRQn_Type) 42) /* MIPICSI GST (Generic Short Packet interrupt) */
        /* The number of entries required for the ICU vector table. */
        #define BSP_ICU_VECTOR_NUM_ENTRIES (43)

        #ifdef __cplusplus
        }
        #endif
        #endif /* VECTOR_DATA_H */