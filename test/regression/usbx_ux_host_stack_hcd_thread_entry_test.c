/* This test is designed to test the _ux_host_stack_hcd_thread_entry.  */

#include <stdio.h>
#include "tx_api.h"
#include "ux_api.h"
#include "ux_system.h"
#include "ux_utility.h"
#include "ux_host_stack.h"
#include "ux_host_class_dpump.h"
#include "ux_device_class_dpump.h"


/* Define USBX test constants.  */

#define UX_TEST_STACK_SIZE      4096
#define UX_TEST_BUFFER_SIZE     2048
#define UX_TEST_RUN             1
#define UX_TEST_MEMORY_SIZE     (64*1024)


/* Define the counters used in the test application...  */

static ULONG                           thread_0_counter;
static ULONG                           thread_1_counter;
static ULONG                           error_counter;

static ULONG                           hcd_thread_counter[UX_MAX_HCD];

static ULONG                           error_callback_ignore = UX_FALSE;


/* Define USBX test global variables.  */

static unsigned char                   host_out_buffer[UX_HOST_CLASS_DPUMP_PACKET_SIZE];
static unsigned char                   host_in_buffer[UX_HOST_CLASS_DPUMP_PACKET_SIZE];
static unsigned char                   slave_buffer[UX_HOST_CLASS_DPUMP_PACKET_SIZE];

static UX_HOST_CLASS                   *class_driver;
static UX_HOST_CLASS_DPUMP             *dpump;
static UX_SLAVE_CLASS_DPUMP            *dpump_slave;


#define DEVICE_FRAMEWORK_LENGTH_FULL_SPEED 50
static UCHAR device_framework_full_speed[] = {

    /* Device descriptor */
        0x12, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 0x08,
        0xec, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01,

    /* Configuration descriptor */
        0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xc0,
        0x32,

    /* Interface descriptor */
        0x09, 0x04, 0x00, 0x00, 0x02, 0x99, 0x99, 0x99,
        0x00,

    /* Endpoint descriptor (Bulk Out) */
        0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,

    /* Endpoint descriptor (Bulk In) */
        0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00
    };


#define DEVICE_FRAMEWORK_LENGTH_HIGH_SPEED 60
static UCHAR device_framework_high_speed[] = {

    /* Device descriptor */
        0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
        0x0a, 0x07, 0x25, 0x40, 0x01, 0x00, 0x01, 0x02,
        0x03, 0x01,

    /* Device qualifier descriptor */
        0x0a, 0x06, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
        0x01, 0x00,

    /* Configuration descriptor */
        0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xc0,
        0x32,

    /* Interface descriptor */
        0x09, 0x04, 0x00, 0x00, 0x02, 0x99, 0x99, 0x99,
        0x00,

    /* Endpoint descriptor (Bulk Out) */
        0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00,

    /* Endpoint descriptor (Bulk In) */
        0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00
    };

    /* String Device Framework :
     Byte 0 and 1 : Word containing the language ID : 0x0904 for US
     Byte 2       : Byte containing the index of the descriptor
     Byte 3       : Byte containing the length of the descriptor string
    */

#define STRING_FRAMEWORK_LENGTH 38
static UCHAR string_framework[] = {

    /* Manufacturer string descriptor : Index 1 */
        0x09, 0x04, 0x01, 0x0c,
        0x45, 0x78, 0x70, 0x72,0x65, 0x73, 0x20, 0x4c,
        0x6f, 0x67, 0x69, 0x63,

    /* Product string descriptor : Index 2 */
        0x09, 0x04, 0x02, 0x0c,
        0x44, 0x61, 0x74, 0x61, 0x50, 0x75, 0x6d, 0x70,
        0x44, 0x65, 0x6d, 0x6f,

    /* Serial Number string descriptor : Index 3 */
        0x09, 0x04, 0x03, 0x04,
        0x30, 0x30, 0x30, 0x31
    };


    /* Multiple languages are supported on the device, to add
       a language besides English, the unicode language code must
       be appended to the language_id_framework array and the length
       adjusted accordingly. */
#define LANGUAGE_ID_FRAMEWORK_LENGTH 2
static UCHAR language_id_framework[] = {

    /* English. */
        0x09, 0x04
    };


/* Define prototypes for external Host Controller's (HCDs), classes and clients.  */

static VOID                ux_test_instance_activate(VOID  *dpump_instance);
static VOID                ux_test_instance_deactivate(VOID *dpump_instance);

UINT                       _ux_host_class_dpump_entry(UX_HOST_CLASS_COMMAND *command);
UINT                       _ux_host_class_dpump_write(UX_HOST_CLASS_DPUMP *dpump, UCHAR * data_pointer,
                                    ULONG requested_length, ULONG *actual_length);
UINT                       _ux_host_class_dpump_read (UX_HOST_CLASS_DPUMP *dpump, UCHAR *data_pointer,
                                    ULONG requested_length, ULONG *actual_length);

static TX_THREAD           ux_test_thread_host_simulation;
static TX_THREAD           ux_test_thread_slave_simulation;
static void                ux_test_thread_host_simulation_entry(ULONG);
static void                ux_test_thread_slave_simulation_entry(ULONG);


/* Define the ISR dispatch.  */

extern VOID    (*test_isr_dispatch)(void);


/* Prototype for test control return.  */

void  test_control_return(UINT status);


/* Define the ISR dispatch routine.  */

static void    test_isr(void)
{

    /* For further expansion of interrupt-level testing.  */
}


static VOID error_callback(UINT system_level, UINT system_context, UINT error_code)
{
    if (error_callback_ignore != UX_TRUE &&
        error_code != UX_CONFIGURATION_HANDLE_UNKNOWN)
    {
        /* Failed test.  */
        printf("Error on line %d, system_level: %d, system_context: %d, error code: %d\n", __LINE__, system_level, system_context, error_code);
        test_control_return(1);
    }
}

UINT  _ux_hcd_test_host_entry(UX_HCD *hcd, UINT function, VOID *parameter)
{

UINT                status;


    /* Check the status of the controller.  */
    if (hcd -> ux_hcd_status == UX_UNUSED)
    {

        /* Error trap. */
        _ux_system_error_handler(UX_SYSTEM_LEVEL_THREAD, UX_SYSTEM_CONTEXT_HCD, UX_CONTROLLER_UNKNOWN);

        /* If trace is enabled, insert this event into the trace buffer.  */
        UX_TRACE_IN_LINE_INSERT(UX_TRACE_ERROR, UX_CONTROLLER_UNKNOWN, 0, 0, 0, UX_TRACE_ERRORS, 0, 0)

        return(UX_CONTROLLER_UNKNOWN);
    }

    hcd_thread_counter[hcd -> ux_hcd_io] ++;

    /* look at the function and route it.  */
    switch(function)
    {

    case UX_HCD_GET_PORT_STATUS:

        status =  UX_PORT_INDEX_UNKNOWN;
        break;
    case UX_HCD_GET_FRAME_NUMBER:
    case UX_HCD_DISABLE_CONTROLLER:
    case UX_HCD_ENABLE_PORT:
    case UX_HCD_DISABLE_PORT:
    case UX_HCD_POWER_ON_PORT:
    case UX_HCD_POWER_DOWN_PORT:
    case UX_HCD_SUSPEND_PORT:
    case UX_HCD_RESUME_PORT:
    case UX_HCD_RESET_PORT:
    case UX_HCD_SET_FRAME_NUMBER:
    case UX_HCD_TRANSFER_REQUEST:
    case UX_HCD_TRANSFER_ABORT:
    case UX_HCD_CREATE_ENDPOINT:
    case UX_HCD_DESTROY_ENDPOINT:
    case UX_HCD_RESET_ENDPOINT:
    case UX_HCD_PROCESS_DONE_QUEUE:

        status =  UX_SUCCESS;
        break;


    default:

        /* Error trap. */
        _ux_system_error_handler(UX_SYSTEM_LEVEL_THREAD, UX_SYSTEM_CONTEXT_HCD, UX_FUNCTION_NOT_SUPPORTED);

        /* If trace is enabled, insert this event into the trace buffer.  */
        UX_TRACE_IN_LINE_INSERT(UX_TRACE_ERROR, UX_FUNCTION_NOT_SUPPORTED, 0, 0, 0, UX_TRACE_ERRORS, 0, 0)

        /* Unknown request, return an error.  */
        status =  UX_FUNCTION_NOT_SUPPORTED;
    }

    /* Return completion status.  */
    return(status);
}

static void  _ux_hcd_test_host_signal_event(UX_HCD *hcd)
{
    hcd -> ux_hcd_thread_signal ++;
    _ux_utility_semaphore_put(&_ux_system_host->ux_system_host_hcd_semaphore);
}

UINT  _ux_hcd_test_host_initialize(UX_HCD *hcd)
{

    /* Initialize the function collector for this HCD.  */
    hcd -> ux_hcd_entry_function =  _ux_hcd_test_host_entry;

    /* Set the host controller into the operational state.  */
    hcd -> ux_hcd_status =  UX_HCD_STATUS_OPERATIONAL;

    /* Get the number of ports on the controller. The number of ports needs to be reflected both
       for the generic HCD container and the local sim_host container. In the simulator,
       the number of ports is hardwired to 1 only.  */
    hcd -> ux_hcd_nb_root_hubs =  1;

    /* Something happened on this port. Signal it to the root hub thread.  */
    hcd -> ux_hcd_root_hub_signal[0] =  1;

    /* We need to simulate a Root HUB Status Change for the USB stack since the simulator
       has not root HUB per se.  */
    _ux_utility_semaphore_put(&_ux_system_host -> ux_system_host_enum_semaphore);

    /* Return successful completion.  */
    return(UX_SUCCESS);
}


/* Define what the initial system looks like.  */

#ifdef CTEST
void test_application_define(void *first_unused_memory)
#else
void    usbx_ux_host_stack_hcd_thread_entry_test_application_define(void *first_unused_memory)
#endif
{

UINT status;
CHAR                            *stack_pointer;
CHAR                            *memory_pointer;
UX_SLAVE_CLASS_DPUMP_PARAMETER  parameter;


    /* Inform user.  */
    printf("Running _ux_host_stack_hcd_thread_entry Test........................ ");

    /* Initialize the free memory pointer.  */
    stack_pointer = (CHAR *) first_unused_memory;
    memory_pointer = stack_pointer + (UX_TEST_STACK_SIZE * 2);

    /* Initialize USBX Memory.  */
    status =  ux_system_initialize(memory_pointer, UX_TEST_MEMORY_SIZE, UX_NULL, 0);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #1\n");
        test_control_return(1);
    }

    /* Register the error callback. */
    _ux_utility_error_callback_register(error_callback);

    /* The code below is required for installing the host portion of USBX.  */
    status =  ux_host_stack_initialize(UX_NULL);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #2\n");
        test_control_return(1);
    }

    /* Register all the host class drivers for this USBX implementation.  */
    status =  ux_host_stack_class_register(_ux_system_host_class_dpump_name, ux_host_class_dpump_entry);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #3\n");
        test_control_return(1);
    }

    /* The code below is required for installing the device portion of USBX */
    status =  ux_device_stack_initialize(device_framework_high_speed, DEVICE_FRAMEWORK_LENGTH_HIGH_SPEED,
                                       device_framework_full_speed, DEVICE_FRAMEWORK_LENGTH_FULL_SPEED,
                                       string_framework, STRING_FRAMEWORK_LENGTH,
                                       language_id_framework, LANGUAGE_ID_FRAMEWORK_LENGTH, UX_NULL);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #5\n");
        test_control_return(1);
    }

    /* Set the parameters for callback when insertion/extraction of a Data Pump device.  */
    parameter.ux_slave_class_dpump_instance_activate   =  ux_test_instance_activate;
    parameter.ux_slave_class_dpump_instance_deactivate =  ux_test_instance_deactivate;

    /* Initialize the device dpump class. The class is connected with interface 0 */
    status =  ux_device_stack_class_register(_ux_system_slave_class_dpump_name, _ux_device_class_dpump_entry,
                                               1, 0, &parameter);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #6\n");
        test_control_return(1);
    }

    /* Initialize the simulated device controller.  */
    status =  _ux_dcd_sim_slave_initialize();

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #7\n");
        test_control_return(1);
    }

    /* Register all the USB host controllers available in this system */
    hcd_thread_counter[0] = 0;
    status =  ux_host_stack_hcd_register(_ux_system_host_hcd_simulator_name, ux_hcd_sim_host_initialize,0,0);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #4\n");
        test_control_return(1);
    }

#if UX_MAX_HCD > 1
    /* Register all the USB host controllers available in this system */
    hcd_thread_counter[1] = 0;
    status =  ux_host_stack_hcd_register("hcd_test_driver 1", _ux_hcd_test_host_initialize, 1, 0);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        printf("ERROR #4\n");
        test_control_return(1);
    }
#endif

    /* Create the main host simulation thread.  */
    status =  tx_thread_create(&ux_test_thread_host_simulation, "test host simulation", ux_test_thread_host_simulation_entry, 0,
            stack_pointer, UX_TEST_STACK_SIZE,
            20, 20, 1, TX_AUTO_START);

    /* Check for error.  */
    if (status != TX_SUCCESS)
    {

        printf("ERROR #8\n");
        test_control_return(1);
    }
}


static void  ux_test_thread_host_simulation_entry(ULONG arg)
{

UINT                status;
UX_HOST_CLASS       *class;

    /* Find the main data pump container.  */
    status =  ux_host_stack_class_get(_ux_system_host_class_dpump_name, &class);

    /* Check for error.  */
    if (status != UX_SUCCESS)
    {

        /* DPUMP basic test error.  */
        printf("ERROR #10\n");
        test_control_return(1);
    }

    /* We get the first instance of the data pump device.  */
    do
    {

        status =  ux_host_stack_class_instance_get(class, 0, (VOID **) &dpump);
        tx_thread_relinquish();
    } while (status != UX_SUCCESS);

    /* We still need to wait for the data pump status to be live.  */
    while (dpump -> ux_host_class_dpump_state != UX_HOST_CLASS_INSTANCE_LIVE)
    {

        tx_thread_relinquish();
    }

    /* At this point, the data pump class has been found.  */

#if UX_MAX_HCD > 1
    /* Check if thread entry is called once.  */
    if (hcd_thread_counter[1] != 1)
    {
        printf("ERROR #%d, %d\n", __LINE__, hcd_thread_counter[1]);
        test_control_return(1);
    }

    /* Check if thread entry is called.  */
    _ux_hcd_test_host_signal_event(&_ux_system_host->ux_system_host_hcd_array[1]);
    _ux_utility_delay_ms(10);
    if (hcd_thread_counter[1] != 2)
    {
        printf("ERROR #%d, %d\n", __LINE__, hcd_thread_counter[1]);
        test_control_return(1);
    }

    /* Check if thread entry is still called when first HCD unregistered.  */
    error_callback_ignore = UX_TRUE;
    ux_host_stack_hcd_unregister(_ux_system_host_hcd_simulator_name, 0, 0);
    _ux_utility_delay_ms(10);
    if (_ux_system_host->ux_system_host_hcd_array[0].ux_hcd_status == UX_HCD_STATUS_OPERATIONAL)
    {
        printf("ERROR #%d, HCD unregister fail\n", __LINE__);
        test_control_return(1);
    }

    _ux_hcd_test_host_signal_event(&_ux_system_host->ux_system_host_hcd_array[1]);
    _ux_utility_delay_ms(10);
    if (hcd_thread_counter[1] != 3)
    {
        printf("ERROR #%d, %d\n", __LINE__, hcd_thread_counter[1]);
        test_control_return(1);
    }
#endif

    /* Sleep for a tick to make sure everything is complete.  */
    tx_thread_sleep(1);

    /* Check for errors from other threads.  */
    if (error_counter)
    {

        /* Test error.  */
        printf("ERROR #14\n");
        test_control_return(1);
    }
    else
    {

        /* Successful test.  */
        printf("SUCCESS!\n");
        test_control_return(0);
    }
}

static VOID  ux_test_instance_activate(VOID *dpump_instance)
{

    /* Save the DPUMP instance.  */
    dpump_slave = (UX_SLAVE_CLASS_DPUMP *) dpump_instance;
}

static VOID  ux_test_instance_deactivate(VOID *dpump_instance)
{

    /* Reset the DPUMP instance.  */
    dpump_slave = UX_NULL;
}

