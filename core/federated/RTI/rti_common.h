/**
 * @file
 * @author Edward A. Lee (eal@berkeley.edu)
 * @author Soroush Bateni (soroush@utdallas.edu)
 * @author Erling Jellum (erling.r.jellum@ntnu.no)
 * @author Chadlia Jerad (chadlia.jerad@ensi-uma.tn)
 * @copyright (c) 2020-2023, The University of California at Berkeley
 * License in [BSD 2-clause](https://github.com/lf-lang/reactor-c/blob/main/LICENSE.md)
 * @brief Common declarations for runtime infrastructure (RTI) for scheduling enclaves and distributed Lingua Franca programs.
 * This file declares RTI features that are used by scheduling enclaves as well as federated
 * LF programs.
 */
#if defined STANDALONE_RTI || defined LF_ENCLAVES
#ifndef RTI_COMMON_H
#define RTI_COMMON_H

#include <errno.h>      // Defines perror(), errno
#include <assert.h>
#include "platform.h"   // Platform-specific types and functions
#include "util.h"       // Defines print functions (e.g., lf_print).
#include "tag.h"        // Time-related types and functions.
#include "trace.h"      // Tracing related functions

/** Mode of execution of a federate. */
typedef enum execution_mode_t {
    FAST,
    REALTIME
} execution_mode_t;

/** State of the scheduling node during execution. */
typedef enum scheduling_node_state_t {
    NOT_CONNECTED,  // The scheduling node has not connected.
    GRANTED,        // Most recent MSG_TYPE_NEXT_EVENT_TAG has been granted.
    PENDING         // Waiting for upstream scheduling nodes.
} scheduling_node_state_t;

/**
 * Information about the scheduling nodes coordinated by the RTI.
 * The abstract scheduling node could either be an enclave or a federate.
 * The information includs its runtime state,
 * mode of execution, and connectivity with other scheduling_nodes.
 * The list of upstream and downstream scheduling_nodes does not include
 * those that are connected via a "physical" connection (one
 * denoted with ~>) because those connections do not impose
 * any scheduling constraints.
 */
typedef struct scheduling_node_t {
    uint16_t id;            // ID of this enclave.
    tag_t completed;        // The largest logical tag completed by the federate (or NEVER if no LTC has been received).
    tag_t last_granted;     // The maximum TAG that has been granted so far (or NEVER if none granted)
    tag_t last_provisionally_granted;      // The maximum PTAG that has been provisionally granted (or NEVER if none granted)
    tag_t next_event;       // Most recent NET received from the federate (or NEVER if none received).
    scheduling_node_state_t state;      // State of the federate.
    int* upstream;          // Array of upstream federate ids.
    interval_t* upstream_delay;    // Minimum delay on connections from upstream federates.
    							   // Here, NEVER encodes no delay. 0LL is a microstep delay.
    int num_upstream;              // Size of the array of upstream federates and delays.
    int* downstream;        // Array of downstream federate ids.
    int num_downstream;     // Size of the array of downstream federates.
    execution_mode_t mode;  // FAST or REALTIME.
} scheduling_node_t;

/**
 * Data structure which is common to both the remote standalone RTI and the local RTI used in enclaved execution.
 * rti_remote_t and rti_local_t will "inherit" from this data structure. The first field is an array of pointers 
 * to scheduling_nodes. These will be scheduling_nodes for the local RTI and federates for the remote RTI 
 * 
 */
typedef struct rti_common_t {
    // The scheduling nodes.
    scheduling_node_t **scheduling_nodes;

    // Number of scheduling nodes
    int32_t number_of_scheduling_nodes;

    // RTI's decided stop tag for the scheduling nodes
    tag_t max_stop_tag;

    // Number of scheduling nodes handling stop
    int num_scheduling_nodes_handling_stop;

    // Boolean indicating that tracing is enabled.
    bool tracing_enabled;
    
    // Pointer to a tracing object
    trace_t* trace;

    // Lock for making thread-safe access to the shared state.
    lf_mutex_t* mutex;
} rti_common_t;

typedef struct {
    tag_t tag;           // NEVER if there is no tag advance grant.
    bool is_provisional; // True for PTAG, false for TAG.
} tag_advance_grant_t;

/**
 * @brief Initialize the fields of the rti_common struct. It also stores
 * the pointer to the struct and uses it internally.
 * 
 * @param rti_common 
 */
void initialize_rti_common(rti_common_t * rti_common);

/**
 * An enclave calls this function after it completed a tag. 
 * The function updates the completed tag and check if the downstream scheduling_nodes 
 * are eligible for receiving TAGs.
 * 
 * @param enclave The enclave
 * @param completed The completed tag of the enclave
 */
// FIXME: Prepended with underscore due to conflict with code-generated function...
void _logical_tag_complete(scheduling_node_t* enclave, tag_t completed);

/** 
 * Initialize the reactor- with the specified ID.
 * 
 * @param e The enclave
 * @param id The enclave ID.
 */
void initialize_scheduling_node(scheduling_node_t* e, uint16_t id);

/**
 * For all scheduling_nodes downstream of the specified enclave, determine
 * whether they should be notified of a TAG or PTAG and notify them if so.
 *
 * This assumes the caller holds the mutex.
 *
 * @param e The upstream enclave.
 * @param visited An array of booleans used to determine whether an enclave has
 *  been visited (initially all false).
 */
void notify_downstream_advance_grant_if_safe(scheduling_node_t* e, bool visited[]);

/**
 * Notify a tag advance grant (TAG) message to the specified federate.
 * Do not notify it if a previously sent PTAG was greater or if a
 * previously sent TAG was greater or equal.
 *
 * This function will keep a record of this TAG in the federate's last_granted
 * field.
 *
 * This function assumes that the caller holds the mutex lock.
 * 
 * FIXME: This needs two implementations, one for enclaves and one for federates.
 *
 * @param e The enclave.
 * @param tag The tag to grant.
 */
void notify_tag_advance_grant(scheduling_node_t* e, tag_t tag);

/**
 * @brief Either send to a federate or unblock an enclave to give it a tag.
 * This function requires two different implementations, one for enclaves
 * and one for federates.
 * 
 * This assumes the caller holds the mutex.
 * 
 * @param e The enclave.
 */
void notify_advance_grant_if_safe(scheduling_node_t* e);

/**
 * Nontify a provisional tag advance grant (PTAG) message to the specified enclave.
 * Do not notify it if a previously sent PTAG or TAG was greater or equal.
 *
 * This function will keep a record of this PTAG in the federate's last_provisionally_granted
 * field.
 *
 * This function assumes that the caller holds the mutex lock.
 *
 * FIXME: This needs two implementations, one for enclaves and one for federates.
 *
 * @param e The enclave.
 * @param tag The tag to grant.
 */
void notify_provisional_tag_advance_grant(scheduling_node_t* e, tag_t tag);

/**
 * Determine whether the specified enclave is eligible for a tag advance grant,
 * (TAG) and, if so, return the details. This is called upon receiving a LTC, NET
 * or resign from an upstream enclave.
 *
 * This function calculates the minimum M over
 * all upstream scheduling_nodes of the "after" delay plus the most recently
 * received LTC from that enclave. If M is greater than the
 * most recent TAG to e or greater than or equal to the most
 * recent PTAG, then return TAG(M).
 *
 * If the above conditions do not result in returning a TAG, then find the
 * minimum M of the earliest possible future message from upstream federates.
 * This is calculated by transitively looking at the most recently received
 * NET calls from upstream scheduling_nodes.
 * If M is greater than the NET of e or the most recent PTAG to e, then
 * return a TAG with tag equal to the NET of e or the PTAG.
 * If M is equal to the NET of the federate, then return PTAG(M).
 *
 * This should be called whenever an immediately upstream federate sends to
 * the RTI an LTC (Logical Tag Complete), or when a transitive upstream
 * federate sends a NET (Next Event Tag) message.
 * It is also called when an upstream federate resigns from the federation.
 *
 * This function assumes that the caller holds the mutex lock.
 *
 * @param e The enclave
 * @return If granted, return the tag value and whether it is provisional. 
 *  Otherwise, return the NEVER_TAG.
 */
tag_advance_grant_t tag_advance_grant_if_safe(scheduling_node_t* e);


/**
 * @brief Update the next event tag of an enclave.
 *
 * This will notify downstream scheduling_nodes with a TAG or PTAG if appropriate.
 *
 * This function assumes that the caller is holding the mutex.
 *
 * @param e The enclave.
 * @param next_event_tag The next event tag for e.
 */
void update_scheduling_node_next_event_tag_locked(scheduling_node_t* e, tag_t next_event_tag);

/**
 * Find the earliest tag at which the specified federate may
 * experience its next event. This is the least next event tag (NET)
 * of the specified federate and (transitively) upstream federates
 * (with delays of the connections added). For upstream federates,
 * we assume (conservatively) that federate upstream of those
 * may also send an event. The result will never be less than
 * the completion time of the federate (which may be NEVER,
 * if the federate has not yet completed a logical time).
 *
 * FIXME: This could be made less conservative by building
 * at code generation time a causality interface table indicating
 * which outputs can be triggered by which inputs. For now, we
 * assume any output can be triggered by any input.
 *
 * @param e The enclave.
 * @param candidate A candidate tag (for the first invocation,
 *  this should be fed->next_event).
 * @param visited An array of booleans indicating which federates
 *  have been visited (for the first invocation, this should be
 *  an array of falses of size _RTI.number_of_federates).
 * @return The earliest next event tag of the enclave e.
 */
tag_t transitive_next_event(scheduling_node_t *e, tag_t candidate, bool visited[]);

#endif // RTI_COMMON_H
#endif // STANDALONE_RTI || LF_ENCLAVES
