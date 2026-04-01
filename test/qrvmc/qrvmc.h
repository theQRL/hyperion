/**
 * QRVMC: Quantum Resistant Client-VM Connector API
 *
 * @copyright
 * Copyright 2016 The EVMC Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * @defgroup QRVMC QRVMC
 * @{
 */
#ifndef QRVMC_H
#define QRVMC_H

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 6)
/**
 * Portable declaration of "deprecated" attribute.
 *
 * Available for clang and GCC 6+ compilers. The older GCC compilers know
 * this attribute, but it cannot be applied to enum elements.
 */
#define QRVMC_DEPRECATED __attribute__((deprecated))
#else
#define QRVMC_DEPRECATED
#endif


#include <stdbool.h> /* Definition of bool, true and false. */
#include <stddef.h>  /* Definition of size_t. */
#include <stdint.h>  /* Definition of int64_t, uint64_t. */

#ifdef __cplusplus
extern "C" {
#endif

/* BEGIN Python CFFI declarations */

enum
{
    /**
     * The QRVMC ABI version number of the interface declared in this file.
     *
     * The QRVMC ABI version always equals the major version number of the QRVMC project.
     * The Host SHOULD check if the ABI versions match when dynamically loading VMs.
     *
     * @see @ref versioning
     */
    QRVMC_ABI_VERSION = 1
};


/**
 * The fixed size array of 32 bytes.
 *
 * 32 bytes of data capable of storing e.g. 256-bit hashes.
 */
typedef struct qrvmc_bytes32
{
    /** The 32 bytes. */
    uint8_t bytes[32];
} qrvmc_bytes32;

/**
 * The alias for qrvmc_bytes32 to represent a big-endian 256-bit integer.
 */
typedef struct qrvmc_bytes32 qrvmc_uint256be;

/** Big-endian 160-bit hash suitable for keeping a QRL address. */
typedef struct qrvmc_address
{
    /** The 20 bytes of the hash. */
    uint8_t bytes[20];
} qrvmc_address;

/** The kind of call-like instruction. */
enum qrvmc_call_kind
{
    QRVMC_CALL = 0,         /**< Request CALL. */
    QRVMC_DELEGATECALL = 1, /**< Request DELEGATECALL.
                                The value param ignored. */
    QRVMC_CREATE = 3,       /**< Request CREATE. */
    QRVMC_CREATE2 = 4       /**< Request CREATE2. */
};

/** The flags for ::qrvmc_message. */
enum qrvmc_flags
{
    QRVMC_STATIC = 1 /**< Static call mode. */
};

/**
 * The message describing a QRVM call, including a zero-depth calls from a transaction origin.
 *
 * Most of the fields are modelled by the section 8. Message Call of the Ethereum Yellow Paper.
 */
struct qrvmc_message
{
    /** The kind of the call. For zero-depth calls ::QRVMC_CALL SHOULD be used. */
    enum qrvmc_call_kind kind;

    /**
     * Additional flags modifying the call execution behavior.
     * In the current version the only valid values are ::QRVMC_STATIC or 0.
     */
    uint32_t flags;

    /**
     * The present depth of the message call stack.
     *
     * Defined as `e` in the Yellow Paper.
     */
    int32_t depth;

    /**
     * The amount of gas available to the message execution.
     *
     * Defined as `g` in the Yellow Paper.
     */
    int64_t gas;

    /**
     * The recipient of the message.
     *
     * This is the address of the account which storage/balance/nonce is going to be modified
     * by the message execution. In case of ::QRVMC_CALL, this is also the account where the
     * message value qrvmc_message::value is going to be transferred.
     * For ::QRVMC_DELEGATECALL, this may be different from the qrvmc_message::code_address.
     *
     * Defined as `r` in the Yellow Paper.
     */
    qrvmc_address recipient;

    /**
     * The sender of the message.
     *
     * The address of the sender of a message call defined as `s` in the Yellow Paper.
     * This must be the message recipient of the message at the previous (lower) depth,
     * except for the ::QRVMC_DELEGATECALL where recipient is the 2 levels above the present depth.
     * At the depth 0 this must be the transaction origin.
     */
    qrvmc_address sender;

    /**
     * The message input data.
     *
     * The arbitrary length byte array of the input data of the call,
     * defined as `d` in the Yellow Paper.
     * This MAY be NULL.
     */
    const uint8_t* input_data;

    /**
     * The size of the message input data.
     *
     * If input_data is NULL this MUST be 0.
     */
    size_t input_size;

    /**
     * The amount of Quanta transferred with the message.
     *
     * This is transferred value for ::QRVMC_CALL or apparent value for ::QRVMC_DELEGATECALL.
     * Defined as `v` or `v~` in the Yellow Paper.
     */
    qrvmc_uint256be value;

    /**
     * The optional value used in new contract address construction.
     *
     * Needed only for a Host to calculate created address when kind is ::QRVMC_CREATE2.
     * Ignored in qrvmc_execute_fn().
     */
    qrvmc_bytes32 create2_salt;

    /**
     * The address of the code to be executed.
     *
     * For ::QRVMC_DELEGATECALL this may be different from the qrvmc_message::recipient.
     * Not required when invoking qrvmc_execute_fn(), only when invoking qrvmc_call_fn().
     * Ignored if kind is ::QRVMC_CREATE or ::QRVMC_CREATE2.
     *
     * In case of ::QRVMC_CAPABILITY_PRECOMPILES implementation, this fields should be inspected
     * to identify the requested precompile.
     *
     * Defined as `c` in the Yellow Paper.
     */
    qrvmc_address code_address;
};


/** The transaction and block data for execution. */
struct qrvmc_tx_context
{
    qrvmc_uint256be tx_gas_price;      /**< The transaction gas price. */
    qrvmc_address tx_origin;           /**< The transaction origin account. */
    qrvmc_address block_coinbase;      /**< The miner of the block. */
    int64_t block_number;              /**< The block number. */
    int64_t block_timestamp;           /**< The block timestamp. */
    int64_t block_gas_limit;           /**< The block gas limit. */
    qrvmc_uint256be block_prev_randao; /**< The block previous RANDAO (EIP-4399). */
    qrvmc_uint256be chain_id;          /**< The blockchain's ChainID. */
    qrvmc_uint256be block_base_fee;    /**< The block base fee per gas (EIP-1559, EIP-3198). */
};

/**
 * @struct qrvmc_host_context
 * The opaque data type representing the Host execution context.
 * @see qrvmc_execute_fn().
 */
struct qrvmc_host_context;

/**
 * Get transaction context callback function.
 *
 *  This callback function is used by a QRVM to retrieve the transaction and
 *  block context.
 *
 *  @param      context  The pointer to the Host execution context.
 *  @return              The transaction context.
 */
typedef struct qrvmc_tx_context (*qrvmc_get_tx_context_fn)(struct qrvmc_host_context* context);

/**
 * Get block hash callback function.
 *
 * This callback function is used by a VM to query the hash of the header of the given block.
 * If the information about the requested block is not available, then this is signalled by
 * returning null bytes.
 *
 * @param context  The pointer to the Host execution context.
 * @param number   The block number.
 * @return         The block hash or null bytes
 *                 if the information about the block is not available.
 */
typedef qrvmc_bytes32 (*qrvmc_get_block_hash_fn)(struct qrvmc_host_context* context,
                                                 int64_t number);

/**
 * The execution status code.
 *
 * Successful execution is represented by ::QRVMC_SUCCESS having value 0.
 *
 * Positive values represent failures defined by VM specifications with generic
 * ::QRVMC_FAILURE code of value 1.
 *
 * Status codes with negative values represent VM internal errors
 * not provided by QRVM specifications. These errors MUST not be passed back
 * to the caller. They MAY be handled by the Client in predefined manner
 * (see e.g. ::QRVMC_REJECTED), otherwise internal errors are not recoverable.
 * The generic representant of errors is ::QRVMC_INTERNAL_ERROR but
 * a QRVM implementation MAY return negative status codes that are not defined
 * in the QRVMC documentation.
 *
 * @note
 * In case new status codes are needed, please create an issue or pull request
 * // TODO(now.youtrack.cloud/issue/TE-11)
 * in the QRVMC repository (https://github.com/rgeraldes24/qrvmc).
 */
enum qrvmc_status_code
{
    /** Execution finished with success. */
    QRVMC_SUCCESS = 0,

    /** Generic execution failure. */
    QRVMC_FAILURE = 1,

    /**
     * Execution terminated with REVERT opcode.
     *
     * In this case the amount of gas left MAY be non-zero and additional output
     * data MAY be provided in ::qrvmc_result.
     */
    QRVMC_REVERT = 2,

    /** The execution has run out of gas. */
    QRVMC_OUT_OF_GAS = 3,

    /**
     * The designated INVALID instruction has been hit during execution.
     *
     * The EIP-141 (https://github.com/ethereum/EIPs/blob/master/EIPS/eip-141.md)
     * defines the instruction 0xfe as INVALID instruction to indicate execution
     * abortion coming from high-level languages. This status code is reported
     * in case this INVALID instruction has been encountered.
     */
    QRVMC_INVALID_INSTRUCTION = 4,

    /** An undefined instruction has been encountered. */
    QRVMC_UNDEFINED_INSTRUCTION = 5,

    /**
     * The execution has attempted to put more items on the QRVM stack
     * than the specified limit.
     */
    QRVMC_STACK_OVERFLOW = 6,

    /** Execution of an opcode has required more items on the QRVM stack. */
    QRVMC_STACK_UNDERFLOW = 7,

    /** Execution has violated the jump destination restrictions. */
    QRVMC_BAD_JUMP_DESTINATION = 8,

    /**
     * Tried to read outside memory bounds.
     *
     * An example is RETURNDATACOPY reading past the available buffer.
     */
    QRVMC_INVALID_MEMORY_ACCESS = 9,

    /** Call depth has exceeded the limit (if any) */
    QRVMC_CALL_DEPTH_EXCEEDED = 10,

    /** Tried to execute an operation which is restricted in static mode. */
    QRVMC_STATIC_MODE_VIOLATION = 11,

    /**
     * A call to a precompiled or system contract has ended with a failure.
     *
     * An example: elliptic curve functions handed invalid EC points.
     */
    QRVMC_PRECOMPILE_FAILURE = 12,

    /**
     * Contract validation has failed (e.g. due to QRVM 1.5 jump validity,
     * Casper's purity checker).
     */
    QRVMC_CONTRACT_VALIDATION_FAILURE = 13,

    /**
     * An argument to a state accessing method has a value outside of the
     * accepted range of values.
     */
    QRVMC_ARGUMENT_OUT_OF_RANGE = 14,

    /**
     * A WebAssembly `unreachable` instruction has been hit during execution.
     */
    QRVMC_WASM_UNREACHABLE_INSTRUCTION = 15,

    /**
     * A WebAssembly trap has been hit during execution. This can be for many
     * reasons, including division by zero, validation errors, etc.
     */
    QRVMC_WASM_TRAP = 16,

    /** The caller does not have enough funds for value transfer. */
    QRVMC_INSUFFICIENT_BALANCE = 17,

    /** QRVM implementation generic internal error. */
    QRVMC_INTERNAL_ERROR = -1,

    /**
     * The execution of the given code and/or message has been rejected
     * by the QRVM implementation.
     *
     * This error SHOULD be used to signal that the QRVM is not able to or
     * willing to execute the given code type or message.
     * If a QRVM returns the ::QRVMC_REJECTED status code,
     * the Client MAY try to execute it in other QRVM implementation.
     * For example, the Client tries running a code in the QRVM 1.5. If the
     * code is not supported there, the execution falls back to the QRVM 1.0.
     */
    QRVMC_REJECTED = -2,

    /** The VM failed to allocate the amount of memory needed for execution. */
    QRVMC_OUT_OF_MEMORY = -3
};

/* Forward declaration. */
struct qrvmc_result;

/**
 * Releases resources assigned to an execution result.
 *
 * This function releases memory (and other resources, if any) assigned to the
 * specified execution result making the result object invalid.
 *
 * @param result  The execution result which resources are to be released. The
 *                result itself it not modified by this function, but becomes
 *                invalid and user MUST discard it as well.
 *                This MUST NOT be NULL.
 *
 * @note
 * The result is passed by pointer to avoid (shallow) copy of the ::qrvmc_result
 * struct. Think of this as the best possible C language approximation to
 * passing objects by reference.
 */
typedef void (*qrvmc_release_result_fn)(const struct qrvmc_result* result);

/** The QRVM code execution result. */
struct qrvmc_result
{
    /** The execution status code. */
    enum qrvmc_status_code status_code;

    /**
     * The amount of gas left after the execution.
     *
     * If qrvmc_result::status_code is neither ::QRVMC_SUCCESS nor ::QRVMC_REVERT
     * the value MUST be 0.
     */
    int64_t gas_left;

    /**
     * The refunded gas accumulated from this execution and its sub-calls.
     *
     * The transaction gas refund limit is not applied.
     * If qrvmc_result::status_code is other than ::QRVMC_SUCCESS the value MUST be 0.
     */
    int64_t gas_refund;

    /**
     * The reference to output data.
     *
     * The output contains data coming from RETURN opcode (iff qrvmc_result::code
     * field is ::QRVMC_SUCCESS) or from REVERT opcode.
     *
     * The memory containing the output data is owned by QRVM and has to be
     * freed with qrvmc_result::release().
     *
     * This pointer MAY be NULL.
     * If qrvmc_result::output_size is 0 this pointer MUST NOT be dereferenced.
     */
    const uint8_t* output_data;

    /**
     * The size of the output data.
     *
     * If qrvmc_result::output_data is NULL this MUST be 0.
     */
    size_t output_size;

    /**
     * The method releasing all resources associated with the result object.
     *
     * This method (function pointer) is optional (MAY be NULL) and MAY be set
     * by the VM implementation. If set it MUST be called by the user once to
     * release memory and other resources associated with the result object.
     * Once the resources are released the result object MUST NOT be used again.
     *
     * The suggested code pattern for releasing execution results:
     * @code
     * struct qrvmc_result result = ...;
     * if (result.release)
     *     result.release(&result);
     * @endcode
     *
     * @note
     * It works similarly to C++ virtual destructor. Attaching the release
     * function to the result itself allows VM composition.
     */
    qrvmc_release_result_fn release;

    /**
     * The address of the possibly created contract.
     *
     * The create address may be provided even though the contract creation has failed
     * (qrvmc_result::status_code is not ::QRVMC_SUCCESS). This is useful in situations
     * when the address is observable, e.g. access to it remains warm.
     * In all other cases the address MUST be null bytes.
     */
    qrvmc_address create_address;

    /**
     * Reserved data that MAY be used by a qrvmc_result object creator.
     *
     * This reserved 4 bytes together with 20 bytes from create_address form
     * 24 bytes of memory called "optional data" within qrvmc_result struct
     * to be optionally used by the qrvmc_result object creator.
     *
     * @see qrvmc_result_optional_data, qrvmc_get_optional_data().
     *
     * Also extends the size of the qrvmc_result to 64 bytes (full cache line).
     */
    uint8_t padding[4];
};


/**
 * Check account existence callback function.
 *
 * This callback function is used by the VM to check if
 * there exists an account at given address.
 * @param context  The pointer to the Host execution context.
 * @param address  The address of the account the query is about.
 * @return         true if exists, false otherwise.
 */
typedef bool (*qrvmc_account_exists_fn)(struct qrvmc_host_context* context,
                                        const qrvmc_address* address);

/**
 * Get storage callback function.
 *
 * This callback function is used by a VM to query the given account storage entry.
 *
 * @param context  The Host execution context.
 * @param address  The address of the account.
 * @param key      The index of the account's storage entry.
 * @return         The storage value at the given storage key or null bytes
 *                 if the account does not exist.
 */
typedef qrvmc_bytes32 (*qrvmc_get_storage_fn)(struct qrvmc_host_context* context,
                                              const qrvmc_address* address,
                                              const qrvmc_bytes32* key);


/**
 * The effect of an attempt to modify a contract storage item.
 *
 * See @ref storagestatus for additional information about design of this enum
 * and analysis of the specification.
 *
 * For the purpose of explaining the meaning of each element, the following
 * notation is used:
 * - 0 is zero value,
 * - X != 0 (X is any value other than 0),
 * - Y != 0, Y != X,  (Y is any value other than X and 0),
 * - Z != 0, Z != X, Z != X (Z is any value other than Y and X and 0),
 * - the "o -> c -> v" triple describes the change status in the context of:
 *   - o: original value (cold value before a transaction started),
 *   - c: current storage value,
 *   - v: new storage value to be set.
 *
 * The order of elements follows EIPs introducing net storage gas costs:
 * - EIP-2200: https://eips.ethereum.org/EIPS/eip-2200,
 * - EIP-1283: https://eips.ethereum.org/EIPS/eip-1283.
 */
enum qrvmc_storage_status
{
    /**
     * The new/same value is assigned to the storage item without affecting the cost structure.
     *
     * The storage value item is either:
     * - left unchanged (c == v) or
     * - the dirty value (o != c) is modified again (c != v).
     * This is the group of cases related to minimal gas cost of only accessing warm storage.
     * 0|X   -> 0 -> 0 (current value unchanged)
     * 0|X|Y -> Y -> Y (current value unchanged)
     * 0|X   -> Y -> Z (modified previously added/modified value)
     *
     * This is "catch all remaining" status. I.e. if all other statuses are correctly matched
     * this status should be assigned to all remaining cases.
     */
    QRVMC_STORAGE_ASSIGNED = 0,

    /**
     * A new storage item is added by changing
     * the current clean zero to a nonzero value.
     * 0 -> 0 -> Z
     */
    QRVMC_STORAGE_ADDED = 1,

    /**
     * A storage item is deleted by changing
     * the current clean nonzero to the zero value.
     * X -> X -> 0
     */
    QRVMC_STORAGE_DELETED = 2,

    /**
     * A storage item is modified by changing
     * the current clean nonzero to other nonzero value.
     * X -> X -> Z
     */
    QRVMC_STORAGE_MODIFIED = 3,

    /**
     * A storage item is added by changing
     * the current dirty zero to a nonzero value other than the original value.
     * X -> 0 -> Z
     */
    QRVMC_STORAGE_DELETED_ADDED = 4,

    /**
     * A storage item is deleted by changing
     * the current dirty nonzero to the zero value and the original value is not zero.
     * X -> Y -> 0
     */
    QRVMC_STORAGE_MODIFIED_DELETED = 5,

    /**
     * A storage item is added by changing
     * the current dirty zero to the original value.
     * X -> 0 -> X
     */
    QRVMC_STORAGE_DELETED_RESTORED = 6,

    /**
     * A storage item is deleted by changing
     * the current dirty nonzero to the original zero value.
     * 0 -> Y -> 0
     */
    QRVMC_STORAGE_ADDED_DELETED = 7,

    /**
     * A storage item is modified by changing
     * the current dirty nonzero to the original nonzero value other than the current value.
     * X -> Y -> X
     */
    QRVMC_STORAGE_MODIFIED_RESTORED = 8
};


/**
 * Set storage callback function.
 *
 * This callback function is used by a VM to update the given account storage entry.
 * The VM MUST make sure that the account exists. This requirement is only a formality because
 * VM implementations only modify storage of the account of the current execution context
 * (i.e. referenced by qrvmc_message::recipient).
 *
 * @param context  The pointer to the Host execution context.
 * @param address  The address of the account.
 * @param key      The index of the storage entry.
 * @param value    The value to be stored.
 * @return         The effect on the storage item.
 */
typedef enum qrvmc_storage_status (*qrvmc_set_storage_fn)(struct qrvmc_host_context* context,
                                                          const qrvmc_address* address,
                                                          const qrvmc_bytes32* key,
                                                          const qrvmc_bytes32* value);

/**
 * Get balance callback function.
 *
 * This callback function is used by a VM to query the balance of the given account.
 *
 * @param context  The pointer to the Host execution context.
 * @param address  The address of the account.
 * @return         The balance of the given account or 0 if the account does not exist.
 */
typedef qrvmc_uint256be (*qrvmc_get_balance_fn)(struct qrvmc_host_context* context,
                                                const qrvmc_address* address);

/**
 * Get code size callback function.
 *
 * This callback function is used by a VM to get the size of the code stored
 * in the account at the given address.
 *
 * @param context  The pointer to the Host execution context.
 * @param address  The address of the account.
 * @return         The size of the code in the account or 0 if the account does not exist.
 */
typedef size_t (*qrvmc_get_code_size_fn)(struct qrvmc_host_context* context,
                                         const qrvmc_address* address);

/**
 * Get code hash callback function.
 *
 * This callback function is used by a VM to get the keccak256 hash of the code stored
 * in the account at the given address. For existing accounts not having a code, this
 * function returns keccak256 hash of empty data.
 *
 * @param context  The pointer to the Host execution context.
 * @param address  The address of the account.
 * @return         The hash of the code in the account or null bytes if the account does not exist.
 */
typedef qrvmc_bytes32 (*qrvmc_get_code_hash_fn)(struct qrvmc_host_context* context,
                                                const qrvmc_address* address);

/**
 * Copy code callback function.
 *
 * This callback function is used by a QRVM to request a copy of the code
 * of the given account to the memory buffer provided by the QRVM.
 * The Client MUST copy the requested code, starting with the given offset,
 * to the provided memory buffer up to the size of the buffer or the size of
 * the code, whichever is smaller.
 *
 * @param context      The pointer to the Host execution context. See ::qrvmc_host_context.
 * @param address      The address of the account.
 * @param code_offset  The offset of the code to copy.
 * @param buffer_data  The pointer to the memory buffer allocated by the QRVM
 *                     to store a copy of the requested code.
 * @param buffer_size  The size of the memory buffer.
 * @return             The number of bytes copied to the buffer by the Client.
 */
typedef size_t (*qrvmc_copy_code_fn)(struct qrvmc_host_context* context,
                                     const qrvmc_address* address,
                                     size_t code_offset,
                                     uint8_t* buffer_data,
                                     size_t buffer_size);

/**
 * Log callback function.
 *
 * This callback function is used by a QRVM to inform about a LOG that happened
 * during a QRVM bytecode execution.
 *
 * @param context       The pointer to the Host execution context. See ::qrvmc_host_context.
 * @param address       The address of the contract that generated the log.
 * @param data          The pointer to unindexed data attached to the log.
 * @param data_size     The length of the data.
 * @param topics        The pointer to the array of topics attached to the log.
 * @param topics_count  The number of the topics. Valid values are between 0 and 4 inclusively.
 */
typedef void (*qrvmc_emit_log_fn)(struct qrvmc_host_context* context,
                                  const qrvmc_address* address,
                                  const uint8_t* data,
                                  size_t data_size,
                                  const qrvmc_bytes32 topics[],
                                  size_t topics_count);

/**
 * Access status per EIP-2929: Gas cost increases for state access opcodes.
 */
enum qrvmc_access_status
{
    /**
     * The entry hasn't been accessed before – it's the first access.
     */
    QRVMC_ACCESS_COLD = 0,

    /**
     * The entry is already in accessed_addresses or accessed_storage_keys.
     */
    QRVMC_ACCESS_WARM = 1
};

/**
 * Access account callback function.
 *
 * This callback function is used by a VM to add the given address
 * to accessed_addresses substate (EIP-2929).
 *
 * @param context  The Host execution context.
 * @param address  The address of the account.
 * @return         QRVMC_ACCESS_WARM if accessed_addresses already contained the address
 *                 or QRVMC_ACCESS_COLD otherwise.
 */
typedef enum qrvmc_access_status (*qrvmc_access_account_fn)(struct qrvmc_host_context* context,
                                                            const qrvmc_address* address);

/**
 * Access storage callback function.
 *
 * This callback function is used by a VM to add the given account storage entry
 * to accessed_storage_keys substate (EIP-2929).
 *
 * @param context  The Host execution context.
 * @param address  The address of the account.
 * @param key      The index of the account's storage entry.
 * @return         QRVMC_ACCESS_WARM if accessed_storage_keys already contained the key
 *                 or QRVMC_ACCESS_COLD otherwise.
 */
typedef enum qrvmc_access_status (*qrvmc_access_storage_fn)(struct qrvmc_host_context* context,
                                                            const qrvmc_address* address,
                                                            const qrvmc_bytes32* key);

/**
 * Pointer to the callback function supporting QRVM calls.
 *
 * @param context  The pointer to the Host execution context.
 * @param msg      The call parameters.
 * @return         The result of the call.
 */
typedef struct qrvmc_result (*qrvmc_call_fn)(struct qrvmc_host_context* context,
                                             const struct qrvmc_message* msg);

/**
 * The Host interface.
 *
 * The set of all callback functions expected by VM instances. This is C
 * realisation of vtable for OOP interface (only virtual methods, no data).
 * Host implementations SHOULD create constant singletons of this (similarly
 * to vtables) to lower the maintenance and memory management cost.
 */
struct qrvmc_host_interface
{
    /** Check account existence callback function. */
    qrvmc_account_exists_fn account_exists;

    /** Get storage callback function. */
    qrvmc_get_storage_fn get_storage;

    /** Set storage callback function. */
    qrvmc_set_storage_fn set_storage;

    /** Get balance callback function. */
    qrvmc_get_balance_fn get_balance;

    /** Get code size callback function. */
    qrvmc_get_code_size_fn get_code_size;

    /** Get code hash callback function. */
    qrvmc_get_code_hash_fn get_code_hash;

    /** Copy code callback function. */
    qrvmc_copy_code_fn copy_code;

    /** Call callback function. */
    qrvmc_call_fn call;

    /** Get transaction context callback function. */
    qrvmc_get_tx_context_fn get_tx_context;

    /** Get block hash callback function. */
    qrvmc_get_block_hash_fn get_block_hash;

    /** Emit log callback function. */
    qrvmc_emit_log_fn emit_log;

    /** Access account callback function. */
    qrvmc_access_account_fn access_account;

    /** Access storage callback function. */
    qrvmc_access_storage_fn access_storage;
};


/* Forward declaration. */
struct qrvmc_vm;

/**
 * Destroys the VM instance.
 *
 * @param vm  The VM instance to be destroyed.
 */
typedef void (*qrvmc_destroy_fn)(struct qrvmc_vm* vm);

/**
 * Possible outcomes of qrvmc_set_option.
 */
enum qrvmc_set_option_result
{
    QRVMC_SET_OPTION_SUCCESS = 0,
    QRVMC_SET_OPTION_INVALID_NAME = 1,
    QRVMC_SET_OPTION_INVALID_VALUE = 2
};

/**
 * Configures the VM instance.
 *
 * Allows modifying options of the VM instance.
 * Options:
 * - code cache behavior: on, off, read-only, ...
 * - optimizations,
 *
 * @param vm     The VM instance to be configured.
 * @param name   The option name. NULL-terminated string. Cannot be NULL.
 * @param value  The new option value. NULL-terminated string. Cannot be NULL.
 * @return       The outcome of the operation.
 */
typedef enum qrvmc_set_option_result (*qrvmc_set_option_fn)(struct qrvmc_vm* vm,
                                                            char const* name,
                                                            char const* value);


/**
 * QRVM revision.
 *
 * The revision of the QRVM specification based on the QRL
 * upgrade / hard fork codenames.
 */
enum qrvmc_revision
{
    /**
     * The Zond revision.
     *
     * https://github.com/ethereum/execution-specs/blob/master/network-upgrades/mainnet-upgrades/shanghai.md
     */
    QRVMC_ZOND = 1,

    /** The maximum QRVM revision supported. */
    QRVMC_MAX_REVISION = QRVMC_ZOND,

    /**
     * The latest known QRVM revision with finalized specification.
     *
     * This is handy for QRVM tools to always use the latest revision available.
     */
    QRVMC_LATEST_STABLE_REVISION = QRVMC_ZOND
};


/**
 * Executes the given code using the input from the message.
 *
 * This function MAY be invoked multiple times for a single VM instance.
 *
 * @param vm         The VM instance. This argument MUST NOT be NULL.
 * @param host       The Host interface. This argument MUST NOT be NULL unless
 *                   the @p vm has the ::QRVMC_CAPABILITY_PRECOMPILES capability.
 * @param context    The opaque pointer to the Host execution context.
 *                   This argument MAY be NULL. The VM MUST pass the same
 *                   pointer to the methods of the @p host interface.
 *                   The VM MUST NOT dereference the pointer.
 * @param rev        The requested QRVM specification revision.
 * @param msg        The call parameters. See ::qrvmc_message. This argument MUST NOT be NULL.
 * @param code       The reference to the code to be executed. This argument MAY be NULL.
 * @param code_size  The length of the code. If @p code is NULL this argument MUST be 0.
 * @return           The execution result.
 */
typedef struct qrvmc_result (*qrvmc_execute_fn)(struct qrvmc_vm* vm,
                                                const struct qrvmc_host_interface* host,
                                                struct qrvmc_host_context* context,
                                                enum qrvmc_revision rev,
                                                const struct qrvmc_message* msg,
                                                uint8_t const* code,
                                                size_t code_size);

/**
 * Possible capabilities of a VM.
 */
enum qrvmc_capabilities
{
    /**
     * The VM is capable of executing QRVM1 bytecode.
     */
    QRVMC_CAPABILITY_QRVM1 = (1u << 0),

    /**
     * The VM is capable of executing the precompiled contracts
     * defined for the range of code addresses.
     *
     * The EIP-1352 (https://eips.ethereum.org/EIPS/eip-1352) specifies
     * the range 0x000...0000 - 0x000...ffff of addresses
     * reserved for precompiled and system contracts.
     *
     * This capability is **experimental** and MAY be removed without notice.
     */
    QRVMC_CAPABILITY_PRECOMPILES = (1u << 2)
};

/**
 * Alias for unsigned integer representing a set of bit flags of QRVMC capabilities.
 *
 * @see qrvmc_capabilities
 */
typedef uint32_t qrvmc_capabilities_flagset;

/**
 * Return the supported capabilities of the VM instance.
 *
 * This function MAY be invoked multiple times for a single VM instance,
 * and its value MAY be influenced by calls to qrvmc_vm::set_option.
 *
 * @param vm  The VM instance.
 * @return    The supported capabilities of the VM. @see qrvmc_capabilities.
 */
typedef qrvmc_capabilities_flagset (*qrvmc_get_capabilities_fn)(struct qrvmc_vm* vm);


/**
 * The VM instance.
 *
 * Defines the base struct of the VM implementation.
 */
struct qrvmc_vm
{
    /**
     * QRVMC ABI version implemented by the VM instance.
     *
     * Can be used to detect ABI incompatibilities.
     * The QRVMC ABI version represented by this file is in ::QRVMC_ABI_VERSION.
     */
    const int abi_version;

    /**
     * The name of the QRVMC VM implementation.
     *
     * It MUST be a NULL-terminated not empty string.
     * The content MUST be UTF-8 encoded (this implies ASCII encoding is also allowed).
     */
    const char* name;

    /**
     * The version of the QRVMC VM implementation, e.g. "1.2.3b4".
     *
     * It MUST be a NULL-terminated not empty string.
     * The content MUST be UTF-8 encoded (this implies ASCII encoding is also allowed).
     */
    const char* version;

    /**
     * Pointer to function destroying the VM instance.
     *
     * This is a mandatory method and MUST NOT be set to NULL.
     */
    qrvmc_destroy_fn destroy;

    /**
     * Pointer to function executing a code by the VM instance.
     *
     * This is a mandatory method and MUST NOT be set to NULL.
     */
    qrvmc_execute_fn execute;

    /**
     * A method returning capabilities supported by the VM instance.
     *
     * The value returned MAY change when different options are set via the set_option() method.
     *
     * A Client SHOULD only rely on the value returned if it has queried it after
     * it has called the set_option().
     *
     * This is a mandatory method and MUST NOT be set to NULL.
     */
    qrvmc_get_capabilities_fn get_capabilities;

    /**
     * Optional pointer to function modifying VM's options.
     *
     * If the VM does not support this feature the pointer can be NULL.
     */
    qrvmc_set_option_fn set_option;
};

/* END Python CFFI declarations */

#ifdef QRVMC_DOCUMENTATION
/**
 * Example of a function creating an instance of an example QRVM implementation.
 *
 * Each QRVM implementation MUST provide a function returning a QRVM instance.
 * The function SHOULD be named `qrvmc_create_<vm-name>(void)`. If the VM name contains hyphens
 * replaces them with underscores in the function names.
 *
 * @par Binaries naming convention
 * For VMs distributed as shared libraries, the name of the library SHOULD match the VM name.
 * The convetional library filename prefixes and extensions SHOULD be ignored by the Client.
 * For example, the shared library with the "beta-interpreter" implementation may be named
 * `libbeta-interpreter.so`.
 *
 * @return  The VM instance or NULL indicating instance creation failure.
 */
struct qrvmc_vm* qrvmc_create_example_vm(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
/** @} */
