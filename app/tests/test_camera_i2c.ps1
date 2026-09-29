param([string]$Compiler = 'D:\MinGW\bin\gcc.exe')

$ErrorActionPreference = 'Stop'
$driverPath = Join-Path $PSScriptRoot '../mtk3_bsp2/sysdepend/ra_fsp/device/hal_i2c/hal_i2c.c'
$driverText = Get-Content $driverPath -Raw
$functions = foreach ($signature in @('void HAL_I2C_Callback', 'EXPORT ER hal_i2c_read_reg16', 'EXPORT ER hal_i2c_write_reg16')) {
    $body = [regex]::Match($driverText, '(?ms)^' + [regex]::Escape($signature) + '\([^\r\n]*\).*?^}').Value
    if (!$body) { throw "Production function not found: $signature" }
    $body
}

$mockHeader = @'
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef int ER;
typedef int ID;
typedef unsigned UW;
typedef unsigned UINT;
typedef unsigned char UB;
typedef int fsp_err_t;
#define EXPORT
#define ENTER_TASK_INDEPENDENT
#define LEAVE_TASK_INDEPENDENT
#define E_OK 0
#define E_IO (-5)
#define E_ID (-18)
#define DEV_HAL_I2C_UNITNM 1
#define DEV_HAL_I2C_TMOUT 1000
#define TWF_ANDW 1
#define TWF_BITCLR 2
#define FSP_SUCCESS 0
#define I2C_MASTER_ADDR_MODE_7BIT 0
#define I2C_MASTER_EVENT_TX_COMPLETE 1
#define I2C_MASTER_EVENT_RX_COMPLETE 2
#define get_dcb_ptr(unit) (&control)
typedef struct { void *hi2c; ID devid; UINT unit; ER err; } T_HAL_I2C_DCB;
typedef struct { void *p_context; int event; } i2c_master_callback_args_t;
static T_HAL_I2C_DCB control = {NULL, 7, 0, E_OK};
static ID id_flgid = 1;
static int address_result, write_result, read_result, wait_calls, read_calls;
static int wait_results[2], events[2];
void HAL_I2C_Callback(i2c_master_callback_args_t *args);
static ID tk_oref_dev(ID descriptor, void *unused) { return descriptor; }
static ER tk_clr_flg(ID flag, UINT mask) { return E_OK; }
static ER tk_set_flg(ID flag, UINT mask) {
    int event = events[wait_calls - 1];
    assert(control.err == ((event == I2C_MASTER_EVENT_TX_COMPLETE || event == I2C_MASTER_EVENT_RX_COMPLETE) ? E_OK : E_IO));
    return E_OK;
}
static fsp_err_t R_IIC_MASTER_SlaveAddressSet(void *handle, UW address, int mode) {
    assert(address == 0x3c);
    return address_result;
}
static fsp_err_t R_IIC_MASTER_Write(void *handle, UB *data, size_t size, bool restart) {
    assert(data[0] == 0x30 && data[1] == 0x08);
    assert(size == (restart ? 2 : 3));
    if (!restart) assert(data[2] == 0x42);
    return write_result;
}
static fsp_err_t R_IIC_MASTER_Read(void *handle, UB *data, size_t size, bool restart) {
    ++read_calls;
    *data = 0x42;
    return read_result;
}
static ER tk_wai_flg(ID flag, UINT mask, int mode, UINT *result, int timeout) {
    int index = wait_calls++;
    if (wait_results[index] < 0) return wait_results[index];
    i2c_master_callback_args_t args = {&control, events[index]};
    HAL_I2C_Callback(&args);
    return E_OK;
}
static void reset_mock(void) {
    address_result = write_result = read_result = wait_calls = read_calls = 0;
    wait_results[0] = wait_results[1] = E_OK;
    events[0] = I2C_MASTER_EVENT_TX_COMPLETE;
    events[1] = I2C_MASTER_EVENT_RX_COMPLETE;
    control.err = E_IO;
}
'@

$testMain = @'
int main(void) {
    UB value = 0;
    reset_mock();
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_OK);
    assert(value == 0x42 && read_calls == 1);
    reset_mock();
    assert(hal_i2c_write_reg16(7, 0x3c, 0x3008, 0x42) == E_OK);
    reset_mock(); events[0] = 99;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    assert(read_calls == 0);
    reset_mock(); events[1] = 99;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    reset_mock(); events[0] = 99;
    assert(hal_i2c_write_reg16(7, 0x3c, 0x3008, 0x42) == E_IO);
    reset_mock(); wait_results[0] = -50;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    assert(read_calls == 0);
    reset_mock(); wait_results[1] = -50;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    reset_mock(); wait_results[0] = -50;
    assert(hal_i2c_write_reg16(7, 0x3c, 0x3008, 0x42) == E_IO);
    reset_mock(); address_result = 1;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    assert(wait_calls == 0);
    reset_mock(); address_result = 1;
    assert(hal_i2c_write_reg16(7, 0x3c, 0x3008, 0x42) == E_IO);
    assert(wait_calls == 0);
    reset_mock(); write_result = 1;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    assert(wait_calls == 0);
    reset_mock(); read_result = 1;
    assert(hal_i2c_read_reg16(7, 0x3c, 0x3008, &value) == E_IO);
    reset_mock(); write_result = 1;
    assert(hal_i2c_write_reg16(7, 0x3c, 0x3008, 0x42) == E_IO);
    reset_mock();
    assert(hal_i2c_read_reg16(8, 0x3c, 0x3008, &value) == E_ID);
    reset_mock();
    assert(hal_i2c_write_reg16(8, 0x3c, 0x3008, 0x42) == E_ID);
    puts("PASS: 15 camera I2C cases, including result-before-notification ordering");
    return 0;
}
'@

$testBinary = Join-Path $env:TEMP ('camera-i2c-' + [guid]::NewGuid().ToString() + '.exe')
try {
    ($mockHeader + "`n" + ($functions -join "`n") + "`n" + $testMain) |
        & $Compiler -x c -std=c99 -Wall -Wextra -Wno-unused-parameter -o $testBinary -
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & $testBinary
    if ($LASTEXITCODE -ne 0) { throw 'Camera I2C regression failed' }
}
finally {
    if (Test-Path $testBinary) { Remove-Item -LiteralPath $testBinary }
}