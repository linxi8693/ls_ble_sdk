#include "spi_flash.h"
#include "le501x.h"
#include "lscache.h"
#include "compile_flag.h"
#include "reg_rcc.h"
#include "field_manipulate.h"
#include "sdk_config.h"
#include "platform.h"
#include "reg_lsgpio.h"
#include "reg_syscfg.h"
#include "reg_rf.h"
#include "ls_ble.h"
#include "ls_dbg.h"
#include "log.h"
#include "common.h"
#include "systick.h"
#include "prf_fotas.h"
#include "cpu.h"
#define FLASH_SECTOR_SIZE (4096)
#define APP_IMAGE_BASE_OFFSET (0x24)
#define FOTA_IMAGE_BASE_OFFSET (0x28)
#define OTA_COPY_STATUS_OFFSET (OTA_INFO_OFFSET + FLASH_PAGE_SIZE)

static void swd_pull_down()
{
    MODIFY_REG(LSGPIOB->PUPD,GPIO_PUPD5_MASK|GPIO_PUPD6_MASK,2<<GPIO_PUPD5_POS | 2<<GPIO_PUPD6_POS);
}

uint16_t trim_head_load()
{
    uint16_t head[8];
    uint16_t version;
    spi_flash_read_security_area(1, 0,(void *)head, sizeof(head));
    if(head[0]== (uint16_t)~head[1] && head[2]==(uint16_t)~head[3])
    {
        version = head[0];
        return version;
    }else
    {
        return 0;
    }
    
}

void trim_version_4100_load()
{
    uint16_t buf[10];
    spi_flash_read_security_area(1, 0x10,(void *)buf, sizeof(buf));
    uint8_t i;
    bool trim_valid = true;
    for(i=0;i<10;i+=2)
    {
        if(buf[i]!=(uint16_t)~buf[i+1])
        {
            trim_valid = false;
            break;
        }
    }
    if(trim_valid)
    {
        struct {
            uint16_t bg_vref_fine:2,
                    bg_reg_trim:6,
                    dcdc_vbg_vctl:4,
                    lvd_ref:3;
        } *cal_0 = (void *)&buf[0];
        struct {
            uint16_t hpldo_trim:2,
                    res:6,
                    ldo_tx_trim:3,
                    res2:1,
                    ldo_rx_trim:3;
        } *cal_1 = (void *)&buf[2];
        struct {
            uint16_t xo16m_cap_trim:6,
                    xo16m_adj:2;
        } *cal_2 = (void *)&buf[4];
        struct {
            uint16_t osc_rc24m_cal:15;
        } *cal_3 = (void *)&buf[6];
        struct {
            uint16_t lpldo_trim:3;
        } *cal_4 = (void *)&buf[8];
        (void)cal_4;
        REG_FIELD_WR(SYSCFG->DCDC, SYSCFG_VBG_VCTL, cal_0->dcdc_vbg_vctl);
        MODIFY_REG(SYSCFG->ANACFG0, SYSCFG_BG_RES_TRIM_MASK | SYSCFG_BG_VREF_FINE_MASK | SYSCFG_LDO_DG_TRIM_MASK | SYSCFG_LVD_REF_MASK
            , cal_0->bg_reg_trim<<SYSCFG_BG_RES_TRIM_POS | cal_0->bg_vref_fine<<SYSCFG_BG_VREF_FINE_POS 
            | cal_1->hpldo_trim << SYSCFG_LDO_DG_TRIM_POS | cal_0->lvd_ref << SYSCFG_LVD_REF_POS);
        REG_FIELD_WR(SYSCFG->ANACFG1,SYSCFG_XO16M_ADJ,cal_2->xo16m_adj);
        MODIFY_REG(SYSCFG->CFG, SYSCFG_HAI_SEL_MASK | SYSCFG_HAI_IBIAS_SEL_MASK | SYSCFG_HAI_CAL_MASK | SYSCFG_HAI_CAP_MASK,
            cal_3->osc_rc24m_cal);
        MODIFY_REG(RF->REG08,RF_LDO_TX_TRIM_MASK | RF_LDO_RX_TRIM_MASK,
            cal_1->ldo_tx_trim<<RF_LDO_TX_TRIM_POS | cal_1->ldo_rx_trim<<RF_LDO_RX_TRIM_POS);
    }else
    {
        MODIFY_REG(RF->REG08,RF_LDO_TX_TRIM_MASK | RF_LDO_RX_TRIM_MASK,
            4<<RF_LDO_TX_TRIM_POS | 4<<RF_LDO_RX_TRIM_POS);
    }
    REG_FIELD_WR(SYSCFG->ANACFG1,SYSCFG_XO16M_CAP_TRIM,0x20);
    REG_FIELD_WR(SYSCFG->PMU_TRIM,SYSCFG_LDO_LP_TRIM,5);
}

void trim_version_4101_load()
{
    uint16_t buf[10];
    spi_flash_read_security_area(1, 0x10,(void *)buf, sizeof(buf));
    uint8_t i;
    for(i=0;i<10;i+=2)
    {
        if(buf[i]!=(uint16_t)~buf[i+1])
        {
            while(1);
        }
    }
    struct {
        uint16_t bg_vref_fine:2,
                bg_reg_trim:6,
                dcdc_vbg_vctl:4,
                lvd_ref:3;
    } *cal_0 = (void *)&buf[0];
    struct {
        uint16_t hpldo_trim:2,
                res:6,
                ldo_tx_trim:3,
                res2:1,
                ldo_rx_trim:3;
    } *cal_1 = (void *)&buf[2];
    struct {
        uint16_t xo16m_cap_trim:6,
                xo16m_adj:2;
    } *cal_2 = (void *)&buf[4];
    struct {
        uint16_t osc_rc24m_cal:15;
    } *cal_3 = (void *)&buf[6];
    struct {
        uint8_t lpldo_trim0;
        uint8_t lpldo_trim1;
    } *cal_4 = (void *)&buf[8];
    cal_2->xo16m_cap_trim = 0x20;
    REG_FIELD_WR(SYSCFG->DCDC, SYSCFG_VBG_VCTL, cal_0->dcdc_vbg_vctl);
    MODIFY_REG(SYSCFG->ANACFG0, SYSCFG_BG_RES_TRIM_MASK | SYSCFG_BG_VREF_FINE_MASK | SYSCFG_LDO_DG_TRIM_MASK | SYSCFG_LVD_REF_MASK
        , cal_0->bg_reg_trim<<SYSCFG_BG_RES_TRIM_POS | cal_0->bg_vref_fine<<SYSCFG_BG_VREF_FINE_POS 
        | cal_1->hpldo_trim << SYSCFG_LDO_DG_TRIM_POS | cal_0->lvd_ref << SYSCFG_LVD_REF_POS);
    MODIFY_REG(SYSCFG->ANACFG1, SYSCFG_XO16M_ADJ_MASK | SYSCFG_XO16M_CAP_TRIM_MASK, 
        cal_2->xo16m_adj << SYSCFG_XO16M_ADJ_POS | cal_2->xo16m_cap_trim << SYSCFG_XO16M_CAP_TRIM_POS);
    MODIFY_REG(SYSCFG->CFG, SYSCFG_HAI_SEL_MASK | SYSCFG_HAI_IBIAS_SEL_MASK | SYSCFG_HAI_CAL_MASK | SYSCFG_HAI_CAP_MASK,
        cal_3->osc_rc24m_cal);
    REG_FIELD_WR(SYSCFG->PMU_TRIM,SYSCFG_LDO_LP_TRIM,cal_4->lpldo_trim1);

    MODIFY_REG(RF->REG08,RF_LDO_TX_TRIM_MASK | RF_LDO_RX_TRIM_MASK,
        cal_1->ldo_tx_trim<<RF_LDO_TX_TRIM_POS | cal_1->ldo_rx_trim<<RF_LDO_RX_TRIM_POS);
}

void trim_val_load()
{
    RCC->APB1EN |= 1<<RCC_RF_POS | 1<<RCC_MDM2_POS;
    uint16_t version = trim_head_load();
    switch(version)
    {
    case 0x4100:
        trim_version_4100_load();
    break;
    case 0x4101:
        trim_version_4101_load();
    break;
    default:
        REG_FIELD_WR(SYSCFG->ANACFG1, SYSCFG_XO16M_CAP_TRIM,0x20);
        REG_FIELD_WR(SYSCFG->PMU_TRIM,SYSCFG_LDO_LP_TRIM,5);
        MODIFY_REG(RF->REG08,RF_LDO_TX_TRIM_MASK | RF_LDO_RX_TRIM_MASK,
            4<<RF_LDO_TX_TRIM_POS | 4<<RF_LDO_RX_TRIM_POS);
    break;
    }
}

static bool need_foreground_ota()
{
    uint32_t ota_status;
    spi_flash_quad_io_read(OTA_INFO_OFFSET,(uint8_t *)&ota_status, sizeof(ota_status));
    return ota_status!=0xffffffff;
}

static void boot_app(uint32_t base)
{
    uint32_t *msp = (void *)base;
    void (**reset_handler)(void) = (void *)(base + 4);
    __set_MSP(*msp);
    (*reset_handler)();
}

static void fw_copy(struct fota_image_info *ptr,uint32_t image_base)
{
    static uint8_t fw_buf[FLASH_PAGE_SIZE];
    uint16_t i;
    for(i=0;i<CEILING(ptr->size, FLASH_PAGE_SIZE);++i)
    {
        if ((i % (FLASH_SECTOR_SIZE/FLASH_PAGE_SIZE)) == 0)
        {
            spi_flash_sector_erase(image_base - FLASH_BASE_ADDR + i*FLASH_PAGE_SIZE);
        }
        
        spi_flash_quad_io_read(ptr->base - FLASH_BASE_ADDR + i*FLASH_PAGE_SIZE, fw_buf, FLASH_PAGE_SIZE);
        spi_flash_quad_page_program(image_base - FLASH_BASE_ADDR + i*FLASH_PAGE_SIZE,fw_buf, FLASH_PAGE_SIZE);
    }
}

static void ota_settings_erase()
{
    spi_flash_sector_erase(OTA_INFO_OFFSET);
}

static bool ota_copy_info_get(struct fota_image_info *ptr)
{
    spi_flash_quad_io_read(OTA_COPY_STATUS_OFFSET,(uint8_t *)ptr, sizeof(struct fota_image_info));
    if(ptr->base==0xffffffff && ptr->size ==0xffffffff)
    {
        return false;
    }else
    {
        return true;
    }
}

void boot_ram_start(uint32_t exec_addr)
{
    extern uint32_t critical_nested_cnt;
    critical_nested_cnt = 0;
    switch_to_rc32k();
    clk_switch();
    power_up_hardware_modules();
    remove_hw_isolation();
    enter_critical();
    systick_start();
    spi_flash_drv_var_init(false,false);
    spi_flash_init();
    spi_flash_qe_status_read_and_set();
    spi_flash_xip_start();
    lscache_cache_ctrl(0,1);
    swd_pull_down();
    trim_head_load();
    uint32_t image_base;
    struct fota_image_info image;
    image_base = config_word_get(APP_IMAGE_BASE_OFFSET);
    if(ota_copy_info_get(&image))
    {
        fw_copy(&image,image_base);
        ota_settings_erase();
    }
    if(need_foreground_ota())
    {
        image_base = config_word_get(FOTA_IMAGE_BASE_OFFSET);
    }
    // else{
    //     image_base = config_word_get(APP_IMAGE_BASE_OFFSET);
    // }
    boot_app(image_base);
}

void fault(void){}

const uint32_t vector[4] = {
    0xC000,
    (uint32_t)&boot_ram_start,
    (uint32_t)&fault,
    (uint32_t)&fault,
};