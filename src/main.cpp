#include "pico/stdlib.h"
#include "Stepper.h"
#include "hardware/i2c.h"
#include <string.h>
#include <stdio.h>

#define STEP_FREQ 100
#define ENABLE_PIN 28
#define MS1_PIN 27
#define MS2_PIN 26
#define MS3_PIN 22
#define RESET_PIN 21
#define SLEEP_PIN 20
#define STEP_PIN 19
#define DIR_PIN 18
#define COUNTER_PIN 17

#define LED1_PIN 15
#define LED2_PIN 14

#define GPIO_SDA0 4
#define GPIO_SCL0 5
#define GPIO_SDA1 2
#define GPIO_SCL1 3
#define SLAVE_ADDR 52
#define Z_ADDR 56

bool z_arm_in_position = true;
bool start_z_test = false;


// Initalise stepper control object.
Stepper stepper(STEP_FREQ, ENABLE_PIN, RESET_PIN, SLEEP_PIN, STEP_PIN, DIR_PIN, MS1_PIN, MS2_PIN, MS3_PIN, COUNTER_PIN);


// Function to be called when I2C transmission is recieved.
void i2c0_irq_handler()
{
    printf("In the i2c IRQ\n");
    size_t how_many = i2c_get_read_available(i2c0);
    
    uint8_t buf[how_many];

    // Read data transmitted.
    i2c_read_raw_blocking(i2c0, buf, how_many);

    // Set LEDs according to data sent.
    if (buf[0] == 0) {
        gpio_put(LED1_PIN,0);
        gpio_put(LED2_PIN,1);
    }
    else if (buf[0] == 1) {
        gpio_put(LED1_PIN,1);
        gpio_put(LED2_PIN,0);
    }
    else if (buf[0] == 2) {
        gpio_put(LED1_PIN,1);
        gpio_put(LED2_PIN,1);
    }
    else {
        gpio_put(LED1_PIN,0);
        gpio_put(LED2_PIN,0);
    }

    // Clear interrupt.
    i2c0->hw->clr_stop_det;
}


// Create callback function that will handle interupts from GPIO button inputs.
void gpio_callback(uint gpio, uint32_t events)
{
    if (gpio == 0 && events == GPIO_IRQ_EDGE_RISE) {
        stepper.forward();
        printf("Pressing button 1\n");
        gpio_put(LED1_PIN, 1);
        gpio_put(LED2_PIN, 0);
    } else if (gpio == 0 && events == GPIO_IRQ_EDGE_FALL) {
        stepper.stop();
        printf("Releasing button 1\n");
        gpio_put(LED1_PIN, 0);
        gpio_put(LED2_PIN, 0);
    } else if (gpio == 1 && events == GPIO_IRQ_EDGE_RISE) {
        stepper.backward();
        printf("Pressing button 2\n");
        gpio_put(LED1_PIN, 0);
        gpio_put(LED2_PIN, 1);
    } else if (gpio == 1 && events == GPIO_IRQ_EDGE_FALL) {
        stepper.stop();
        printf("Releasing button 2\n");
        gpio_put(LED1_PIN, 0);
        gpio_put(LED2_PIN, 0);
    } else if (gpio == 13 && events == GPIO_IRQ_EDGE_RISE) {
        printf("Pressing button 3\n");
        start_z_test = true;
    }
}


int main(void)
{  
    // Set up USB comms for print debugging.
    stdio_init_all();

    // Set up GPIO input on pins 0, 1, and 13 for control buttons.
    gpio_init(0);
    gpio_set_dir(0, GPIO_IN);
    gpio_pull_down(0);  // Button connects to 3V3, so we need to pull down upon button release.

    gpio_init(1);
    gpio_set_dir(1, GPIO_IN);
    gpio_pull_down(1);  // Same as above.

    gpio_init(13);
    gpio_set_dir(13, GPIO_IN);
    gpio_pull_down(13);

    gpio_init(16);
    gpio_set_dir(16, GPIO_IN);
    gpio_pull_down(16);

    // Set up GPIO outputs for LEDs, off by default.
    gpio_init(LED1_PIN);
    gpio_set_dir(LED1_PIN, GPIO_OUT);
    gpio_put(LED1_PIN, 0);

    gpio_init(LED2_PIN);
    gpio_set_dir(LED2_PIN, GPIO_OUT);
    gpio_put(LED2_PIN, 0);


    // Set up I2C0 as slave.
    i2c_init(i2c0, 100000);
    i2c_set_slave_mode(i2c0, true, SLAVE_ADDR);
    gpio_set_function(GPIO_SDA0, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_SCL0, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_SDA0);
    gpio_pull_up(GPIO_SCL0);


    // Enable the I2C interrupts we want to process. Interrupt on stop signal.
    i2c0->hw->intr_mask = I2C_IC_INTR_MASK_M_STOP_DET_BITS;

    // Only interrupt if we are addressed.
    i2c0->hw->enable = 0;
    hw_set_bits(&i2c0->hw->con, I2C_IC_CON_STOP_DET_IFADDRESSED_BITS);
    i2c0->hw->enable = 1;

    // Set up the interrupt handler to service I2C interrupts.
    irq_set_exclusive_handler(I2C0_IRQ, &i2c0_irq_handler);

    // Enable I2C interrupt.
    irq_set_enabled(I2C0_IRQ, true);


    // set up I2C1 as master.
    i2c_init(i2c1, 100000);
    gpio_set_function(GPIO_SDA1, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_SCL1, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_SDA1);
    gpio_pull_up(GPIO_SCL1);


    // Set up interupts on the button inputs.
    gpio_set_irq_enabled_with_callback(0, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(13, GPIO_IRQ_EDGE_RISE, true);

    uint32_t micron_pos;
    uint8_t data[sizeof(micron_pos)+1];
    data[0] = 0;  // Header
    uint8_t z_response_data[1] = {2};
    int return_val;

    // Loop forever.
    while (true) {
        printf("Working...\n");
        sleep_ms(1000);
        if (start_z_test) {
            printf("Z test started\n");

            micron_pos = 37000;
            memcpy(&data[1], &micron_pos, sizeof(micron_pos));
            printf("Writing %08X in chunks of %02X %02X %02X %02X, with %02X header.\n", micron_pos, data[1], data[2], data[3], data[4], data[0]);
            return_val = i2c_write_blocking(i2c1, Z_ADDR, data, sizeof(data), false);
            printf("Return value is %d\n", return_val);
            z_arm_in_position = false;

            i2c_read_blocking(i2c1, Z_ADDR, z_response_data, sizeof(z_response_data), false);
            if (z_response_data[0] == 0) {
                printf("Z arm not in position\n");
            } else if (z_response_data[0] == 1){
                printf("Z arm is now in position! Applying paste\n");
                z_arm_in_position = true;
                stepper.forward_by(15);
            } else {
                printf("Something's not right\n");
            }

            sleep_ms(2000);

            micron_pos = 0;
            memcpy(&data[1], &micron_pos, sizeof(micron_pos));
            printf("Writing %08X in chunks of %02X %02X %02X %02X, with %02X header.\n", micron_pos, data[1], data[2], data[3], data[4], data[0]);
            return_val = i2c_write_blocking(i2c1, Z_ADDR, data, sizeof(data), false);
            printf("Return value is %d\n", return_val);
            z_arm_in_position = false;

            i2c_read_blocking(i2c1, Z_ADDR, z_response_data, sizeof(z_response_data), false);
            if (z_response_data[0] == 0) {
                printf("Z arm not in position\n");
            } else if (z_response_data[0] == 1){
                printf("Z arm is now in position!\n");
                z_arm_in_position = true;
            } else {
                printf("Something's not right\n");
            }

            start_z_test = false;
        }
    }
}
