// QRVMC: Quantum Resistant Client-VM Connector API.
// Copyright 2018 The EVMC Authors.
// Licensed under the Apache License, Version 2.0.

/**
 * QRVMC Helpers
 *
 * A collection of C helper functions for invoking a VM instance methods.
 * These are convenient for languages where invoking function pointers
 * is "ugly" or impossible (such as Go).
 *
 * @defgroup helpers QRVMC Helpers
 * @{
 */
#pragma once

#include <qrvmc/qrvmc.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#endif

/**
 * Returns true if the VM has a compatible ABI version.
 */
static inline bool qrvmc_is_abi_compatible(struct qrvmc_vm* vm)
{
    return vm->abi_version == QRVMC_ABI_VERSION;
}

/**
 * Returns the name of the VM.
 */
static inline const char* qrvmc_vm_name(struct qrvmc_vm* vm)
{
    return vm->name;
}

/**
 * Returns the version of the VM.
 */
static inline const char* qrvmc_vm_version(struct qrvmc_vm* vm)
{
    return vm->version;
}

/**
 * Checks if the VM has the given capability.
 *
 * @see qrvmc_get_capabilities_fn
 */
static inline bool qrvmc_vm_has_capability(struct qrvmc_vm* vm, enum qrvmc_capabilities capability)
{
    return (vm->get_capabilities(vm) & (qrvmc_capabilities_flagset)capability) != 0;
}

/**
 * Destroys the VM instance.
 *
 * @see qrvmc_destroy_fn
 */
static inline void qrvmc_destroy(struct qrvmc_vm* vm)
{
    vm->destroy(vm);
}

/**
 * Sets the option for the VM, if the feature is supported by the VM.
 *
 * @see qrvmc_set_option_fn
 */
static inline enum qrvmc_set_option_result qrvmc_set_option(struct qrvmc_vm* vm,
                                                            char const* name,
                                                            char const* value)
{
    if (vm->set_option)
        return vm->set_option(vm, name, value);
    return QRVMC_SET_OPTION_INVALID_NAME;
}

/**
 * Executes code in the VM instance.
 *
 * @see qrvmc_execute_fn.
 */
static inline struct qrvmc_result qrvmc_execute(struct qrvmc_vm* vm,
                                                const struct qrvmc_host_interface* host,
                                                struct qrvmc_host_context* context,
                                                enum qrvmc_revision rev,
                                                const struct qrvmc_message* msg,
                                                uint8_t const* code,
                                                size_t code_size)
{
    return vm->execute(vm, host, context, rev, msg, code, code_size);
}

/// The qrvmc_result release function using free() for releasing the memory.
///
/// This function is used in the qrvmc_make_result(),
/// but may be also used in other case if convenient.
///
/// @param result The result object.
static void qrvmc_free_result_memory(const struct qrvmc_result* result)
{
    free((uint8_t*)result->output_data);
}

/// Creates the result from the provided arguments.
///
/// The provided output is copied to memory allocated with malloc()
/// and the qrvmc_result::release function is set to one invoking free().
///
/// In case of memory allocation failure, the result has all fields zeroed
/// and only qrvmc_result::status_code is set to ::QRVMC_OUT_OF_MEMORY internal error.
///
/// @param status_code  The status code.
/// @param gas_left     The amount of gas left.
/// @param gas_refund   The amount of refunded gas.
/// @param output_data  The pointer to the output.
/// @param output_size  The output size.
static inline struct qrvmc_result qrvmc_make_result(enum qrvmc_status_code status_code,
                                                    int64_t gas_left,
                                                    int64_t gas_refund,
                                                    const uint8_t* output_data,
                                                    size_t output_size)
{
    struct qrvmc_result result;
    memset(&result, 0, sizeof(result));

    if (output_size != 0)
    {
        uint8_t* buffer = (uint8_t*)malloc(output_size);

        if (!buffer)
        {
            result.status_code = QRVMC_OUT_OF_MEMORY;
            return result;
        }

        memcpy(buffer, output_data, output_size);
        result.output_data = buffer;
        result.output_size = output_size;
        result.release = qrvmc_free_result_memory;
    }

    result.status_code = status_code;
    result.gas_left = gas_left;
    result.gas_refund = gas_refund;
    return result;
}

/**
 * Releases the resources allocated to the execution result.
 *
 * @param result  The result object to be released. MUST NOT be NULL.
 *
 * @see qrvmc_result::release() qrvmc_release_result_fn
 */
static inline void qrvmc_release_result(struct qrvmc_result* result)
{
    if (result->release)
        result->release(result);
}


/**
 * Helpers for optional storage of qrvmc_result.
 *
 * In some contexts (i.e. qrvmc_result::create_address is unused) objects of
 * type qrvmc_result contains a memory storage that MAY be used by the object
 * owner. This group defines helper types and functions for accessing
 * the optional storage.
 *
 * @defgroup result_optional_storage Result Optional Storage
 * @{
 */

/**
 * The union representing qrvmc_result "optional storage".
 *
 * The qrvmc_result struct contains 24 bytes of optional storage that can be
 * reused by the object creator if the object does not contain
 * qrvmc_result::create_address.
 *
 * A VM implementation MAY use this memory to keep additional data
 * when returning result from qrvmc_execute_fn().
 * The host application MAY use this memory to keep additional data
 * when returning result of performed calls from qrvmc_call_fn().
 *
 * @see qrvmc_get_optional_storage(), qrvmc_get_const_optional_storage().
 */
union qrvmc_result_optional_storage
{
    uint8_t bytes[24]; /**< 24 bytes of optional storage. */
    void* pointer;     /**< Optional pointer. */
};

/** Provides read-write access to qrvmc_result "optional storage". */
static inline union qrvmc_result_optional_storage* qrvmc_get_optional_storage(
    struct qrvmc_result* result)
{
    return (union qrvmc_result_optional_storage*)&result->create_address;
}

/** Provides read-only access to qrvmc_result "optional storage". */
static inline const union qrvmc_result_optional_storage* qrvmc_get_const_optional_storage(
    const struct qrvmc_result* result)
{
    return (const union qrvmc_result_optional_storage*)&result->create_address;
}

/** @} */

/** Returns text representation of the ::qrvmc_status_code. */
static inline const char* qrvmc_status_code_to_string(enum qrvmc_status_code status_code)
{
    switch (status_code)
    {
    case QRVMC_SUCCESS:
        return "success";
    case QRVMC_FAILURE:
        return "failure";
    case QRVMC_REVERT:
        return "revert";
    case QRVMC_OUT_OF_GAS:
        return "out of gas";
    case QRVMC_INVALID_INSTRUCTION:
        return "invalid instruction";
    case QRVMC_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case QRVMC_STACK_OVERFLOW:
        return "stack overflow";
    case QRVMC_STACK_UNDERFLOW:
        return "stack underflow";
    case QRVMC_BAD_JUMP_DESTINATION:
        return "bad jump destination";
    case QRVMC_INVALID_MEMORY_ACCESS:
        return "invalid memory access";
    case QRVMC_CALL_DEPTH_EXCEEDED:
        return "call depth exceeded";
    case QRVMC_STATIC_MODE_VIOLATION:
        return "static mode violation";
    case QRVMC_PRECOMPILE_FAILURE:
        return "precompile failure";
    case QRVMC_CONTRACT_VALIDATION_FAILURE:
        return "contract validation failure";
    case QRVMC_ARGUMENT_OUT_OF_RANGE:
        return "argument out of range";
    case QRVMC_WASM_UNREACHABLE_INSTRUCTION:
        return "wasm unreachable instruction";
    case QRVMC_WASM_TRAP:
        return "wasm trap";
    case QRVMC_INSUFFICIENT_BALANCE:
        return "insufficient balance";
    case QRVMC_INTERNAL_ERROR:
        return "internal error";
    case QRVMC_REJECTED:
        return "rejected";
    case QRVMC_OUT_OF_MEMORY:
        return "out of memory";
    }
    return "<unknown>";
}

/** Returns the name of the ::qrvmc_revision. */
static inline const char* qrvmc_revision_to_string(enum qrvmc_revision rev)
{
    switch (rev)
    {
    case QRVMC_ZOND:
        return "Zond";
    }
    return "<unknown>";
}

/** @} */

#ifdef __cplusplus
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}  // extern "C"
#endif
