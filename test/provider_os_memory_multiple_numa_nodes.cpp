// Copyright (C) 2024 Intel Corporation
// Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "base.hpp"
#include "numa_helpers.h"
#include "test_helpers.h"

#include <numa.h>
#include <numaif.h>
#include <sched.h>

#include <umf/providers/provider_os_memory.h>

static umf_os_memory_provider_params_t UMF_OS_MEMORY_PROVIDER_PARAMS_TEST =
    umfOsMemoryProviderParamsDefault();

std::vector<unsigned> get_available_numa_nodes() {
    UT_ASSERTne(numa_available(), -1);
    UT_ASSERTne(numa_all_nodes_ptr, nullptr);

    std::vector<unsigned> available_numa_nodes;
    // Get all available NUMA nodes numbers.
    printf("All NUMA nodes: ");
    for (size_t i = 0; i < (size_t)numa_max_node() + 1; ++i) {
        if (numa_bitmask_isbitset(numa_all_nodes_ptr, i) == 1) {
            available_numa_nodes.emplace_back((unsigned)i);
            printf("%ld, ", i);
        }
    }
    printf("\n");

    return available_numa_nodes;
}

std::vector<int> get_available_cpus() {
    std::vector<int> available_cpus;
    cpu_set_t *mask = CPU_ALLOC(CPU_SETSIZE);
    CPU_ZERO(mask);

    int ret = sched_getaffinity(0, sizeof(cpu_set_t), mask);
    UT_ASSERTeq(ret, 0);
    // Get all available cpus.
    printf("All CPUs: ");
    for (size_t i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, mask)) {
            available_cpus.emplace_back(i);
            printf("%ld, ", i);
        }
    }
    printf("\n");
    CPU_FREE(mask);

    return available_cpus;
}

void set_all_available_nodemask_bits(bitmask *nodemask) {
    UT_ASSERTne(numa_available(), -1);
    UT_ASSERTne(numa_all_nodes_ptr, nullptr);

    numa_bitmask_clearall(nodemask);

    // Set all available NUMA nodes numbers.
    copy_bitmask_to_bitmask(numa_all_nodes_ptr, nodemask);
}

struct testNuma : testing::Test {
    void SetUp() override {
        if (numa_available() == -1) {
            GTEST_SKIP() << "Test skipped, NUMA not available";
        }
        if (numa_num_task_nodes() <= 1) {
            GTEST_SKIP()
                << "Test skipped, the number of NUMA nodes is less than two";
        }

        nodemask = numa_allocate_nodemask();
        ASSERT_NE(nodemask, nullptr);
    }

    void
    initOsProvider(umf_os_memory_provider_params_t os_memory_provider_params) {
        umf_result_t umf_result;
        umf_result = umfMemoryProviderCreate(umfOsMemoryProviderOps(),
                                             &os_memory_provider_params,
                                             &os_memory_provider);
        ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
        ASSERT_NE(os_memory_provider, nullptr);
    }

    struct bitmask *retrieve_nodemask(void *addr) {
        struct bitmask *retrieved_nodemask = numa_allocate_nodemask();
        UT_ASSERTne(nodemask, nullptr);
        int ret = get_mempolicy(nullptr, retrieved_nodemask->maskp,
                                nodemask->size, addr, MPOL_F_ADDR);
        UT_ASSERTeq(ret, 0);
        return retrieved_nodemask;
    }

    void TearDown() override {
        umf_result_t umf_result;
        if (ptr) {
            umf_result =
                umfMemoryProviderFree(os_memory_provider, ptr, alloc_size);
            ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
        }
        if (os_memory_provider) {
            umfMemoryProviderDestroy(os_memory_provider);
        }
        if (nodemask) {
            numa_bitmask_clearall(nodemask);
            numa_bitmask_free(nodemask);
        }
    }

    size_t alloc_size = 1024;
    void *ptr = nullptr;
    bitmask *nodemask = nullptr;
    umf_memory_provider_handle_t os_memory_provider = nullptr;
};

struct testNumaOnEachNode : testNuma, testing::WithParamInterface<unsigned> {};
struct testNumaOnEachCpu : testNuma, testing::WithParamInterface<int> {};

INSTANTIATE_TEST_SUITE_P(testNumaNodesAllocations, testNumaOnEachNode,
                         ::testing::ValuesIn(get_available_numa_nodes()));

INSTANTIATE_TEST_SUITE_P(testNumaNodesAllocationsAllCpus, testNumaOnEachCpu,
                         ::testing::ValuesIn(get_available_cpus()));

// Test for allocations on numa nodes. It will be executed on each of
// the available numa nodes.
TEST_P(testNumaOnEachNode, checkNumaNodesAllocations) {
    unsigned numa_node_number = GetParam();
    ASSERT_GE(numa_node_number, 0);

    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    os_memory_provider_params.numa_list = &numa_node_number;
    os_memory_provider_params.numa_list_len = 1;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_BIND;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocations on numa nodes with mode preferred. It will be executed
// on each of the available numa nodes.
TEST_P(testNumaOnEachNode, checkModePreferred) {
    unsigned numa_node_number = GetParam();
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    os_memory_provider_params.numa_list = &numa_node_number;
    numa_bitmask_setbit(nodemask, numa_node_number);
    os_memory_provider_params.numa_list_len = 1;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_PREFERRED;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocation on numa node with default mode enabled.
// We explicitly set the bind mode (via set_mempolicy) so it should fall back to it.
// It will be executed on each of the available numa nodes.
TEST_P(testNumaOnEachNode, checkModeDefaultSetMempolicy) {
    unsigned numa_node_number = GetParam();
    numa_bitmask_setbit(nodemask, numa_node_number);
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;
    initOsProvider(os_memory_provider_params);

    long ret = set_mempolicy(MPOL_BIND, nodemask->maskp, nodemask->size);
    ASSERT_EQ(ret, 0);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocations on a single numa node with interleave mode enabled.
// It will be executed on each of the available numa nodes.
TEST_P(testNumaOnEachNode, checkModeInterleaveSingleNode) {
    unsigned numa_node_number = GetParam();

    constexpr int pages_num = 1024;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    os_memory_provider_params.numa_list = &numa_node_number;
    os_memory_provider_params.numa_list_len = 1;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_INTERLEAVE;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result = umfMemoryProviderAlloc(os_memory_provider,
                                        pages_num * page_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, pages_num * page_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocation on numa node with mode preferred and an empty nodeset.
// For the empty nodeset the memory is allocated on the node of the CPU that
// triggered the allocation. It will be executed on each available CPU.
TEST_P(testNumaOnEachCpu, checkModePreferredEmptyNodeset) {
    int cpu = GetParam();
    ASSERT_GE(cpu, 0);

    cpu_set_t *mask = CPU_ALLOC(CPU_SETSIZE);
    CPU_ZERO(mask);

    CPU_SET(cpu, mask);
    int ret = sched_setaffinity(0, sizeof(cpu_set_t), mask);
    CPU_FREE(mask);

    UT_ASSERTeq(ret, 0);

    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_PREFERRED;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // Verify we're on the expected CPU
    int cpu_check = sched_getcpu();
    UT_ASSERTeq(cpu, cpu_check);

    int numa_node_number = numa_node_of_cpu(cpu);
    printf("Got CPU: %d, got numa node: %d\n", cpu, numa_node_number);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocation on numa node with local mode enabled. The memory is
// allocated on the node of the CPU that triggered the allocation.
// It will be executed on each available CPU.
TEST_P(testNumaOnEachCpu, checkModeLocal) {
    int cpu = GetParam();
    cpu_set_t *mask = CPU_ALLOC(CPU_SETSIZE);
    CPU_ZERO(mask);

    CPU_SET(cpu, mask);
    int ret = sched_setaffinity(0, sizeof(cpu_set_t), mask);
    CPU_FREE(mask);

    UT_ASSERTeq(ret, 0);

    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_LOCAL;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // Verify we're on the expected CPU
    int cpu_check = sched_getcpu();
    UT_ASSERTeq(cpu, cpu_check);

    int numa_node_number = numa_node_of_cpu(cpu);
    printf("Got CPU: %d, got numa node: %d\n", cpu, numa_node_number);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocation on numa node with default mode enabled.
// Since no policy is set (via set_mempolicy) it should default to the system-wide
// default policy - it allocates pages on the node of the CPU that triggered
// the allocation.
TEST_F(testNuma, checkModeDefault) {
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    int cpu = sched_getcpu();
    int numa_node_number = numa_node_of_cpu(cpu);
    printf("Got CPU: %d, got numa node: %d\n", cpu, numa_node_number);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    int retrieved_numa_node_number = getNumaNodeByPtr(ptr);
    EXPECT_EQ(retrieved_numa_node_number, numa_node_number);
}

// Test for allocations on numa nodes with interleave mode enabled.
// The page allocations are interleaved across the set of all available nodes.
TEST_F(testNuma, checkModeInterleave) {
    constexpr int pages_num = 1024;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    std::vector<unsigned> numa_nodes = get_available_numa_nodes();
    set_all_available_nodemask_bits(nodemask);

    os_memory_provider_params.numa_list = numa_nodes.data();
    os_memory_provider_params.numa_list_len = numa_nodes.size();
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_INTERLEAVE;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result = umfMemoryProviderAlloc(os_memory_provider,
                                        pages_num * page_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, pages_num * page_size);

    // Test where each page will be allocated.
    // Get the first numa node for ptr; Each next page is expected to be on next nodes.
    size_t index = getNumaNodeByPtr((char *)ptr);
    for (size_t i = 1; i < (size_t)pages_num; i++) {
        index = (index + 1) % numa_nodes.size();
        ASSERT_EQ(numa_nodes[index],
                  getNumaNodeByPtr((char *)ptr + page_size * i));
    }

    bitmask *retrieved_nodemask = retrieve_nodemask(ptr);
    int ret = numa_bitmask_equal(retrieved_nodemask, nodemask);
    numa_bitmask_free(retrieved_nodemask);

    EXPECT_EQ(ret, 1);
}

// Test for allocations on numa nodes with interleave mode enabled and custom part size set.
// The page allocations are interleaved across the set of nodes specified in nodemask.
TEST_F(testNuma, checkModeInterleaveCustomPartSize) {
    constexpr int part_num = 1024;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    size_t part_size = page_size * 100;
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    std::vector<unsigned> numa_nodes = get_available_numa_nodes();

    os_memory_provider_params.numa_list = numa_nodes.data();
    os_memory_provider_params.numa_list_len = numa_nodes.size();
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_INTERLEAVE;
    // part size do not need to be multiple of page size
    os_memory_provider_params.part_size = part_size - 1;
    initOsProvider(os_memory_provider_params);

    size_t size = part_num * part_size;
    umf_result_t umf_result;
    umf_result = umfMemoryProviderAlloc(os_memory_provider, size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, size);
    // Test where each page will be allocated.
    // Get the first numa node for ptr; Each next part is expected to be on next nodes.
    size_t index = getNumaNodeByPtr((char *)ptr);
    for (size_t i = 0; i < (size_t)part_num; i++) {
        for (size_t j = 0; j < part_size; j += page_size) {
            EXPECT_EQ(numa_nodes[index],
                      getNumaNodeByPtr((char *)ptr + part_size * i + j))
                << "for ptr " << ptr << " + " << part_size << " * " << i
                << " + " << j;
        }
        index = (index + 1) % numa_nodes.size();
    }
    umfMemoryProviderFree(os_memory_provider, ptr, size);

    // test allocation smaller then part size
    size = part_size / 2 + 1;
    umf_result = umfMemoryProviderAlloc(os_memory_provider, size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);
    memset(ptr, 0xFF, size);
    EXPECT_EQ(numa_nodes[index], getNumaNodeByPtr(ptr));
    umfMemoryProviderFree(os_memory_provider, ptr, size);
}

// Test for allocations on all numa nodes with BIND mode.
// According to mbind it should go to the closest node.
TEST_F(testNuma, checkModeBindOnAllNodes) {
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    std::vector<unsigned> numa_nodes = get_available_numa_nodes();

    os_memory_provider_params.numa_list = numa_nodes.data();
    os_memory_provider_params.numa_list_len = numa_nodes.size();
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_BIND;
    initOsProvider(os_memory_provider_params);

    umf_result_t umf_result;
    umf_result =
        umfMemoryProviderAlloc(os_memory_provider, alloc_size, 0, &ptr);
    ASSERT_EQ(umf_result, UMF_RESULT_SUCCESS);
    ASSERT_NE(ptr, nullptr);

    // 'ptr' must point to an initialized value before retrieving its numa node
    memset(ptr, 0xFF, alloc_size);
    unsigned retrieved_numa_node_number = (unsigned)getNumaNodeByPtr(ptr);

    int read_cpu = sched_getcpu();
    int read_numa_node = numa_node_of_cpu(read_cpu);
    printf("Got CPU: %d, got numa node: %d\n", read_cpu, read_numa_node);

    // Verify if numa node related to CPU triggering allocation is in the original list
    size_t count = 0;
    for (size_t i = 0; i < numa_nodes.size(); i++) {
        if (retrieved_numa_node_number == numa_nodes[i]) {
            count++;
        }
    }
    EXPECT_EQ(count, 1);
    // ... and it's the one which we expect
    EXPECT_EQ(retrieved_numa_node_number, read_numa_node);
}

// Negative tests for policies with illegal arguments.

// Local mode enabled when numa_list is set.
// For the local mode the nodeset must be empty.
TEST_F(testNuma, checkModeLocalIllegalArgSet) {
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    std::vector<unsigned> numa_nodes = get_available_numa_nodes();

    os_memory_provider_params.numa_list = numa_nodes.data();
    os_memory_provider_params.numa_list_len = numa_nodes.size();
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_LOCAL;

    umf_result_t umf_result;
    umf_result = umfMemoryProviderCreate(umfOsMemoryProviderOps(),
                                         &os_memory_provider_params,
                                         &os_memory_provider);
    ASSERT_EQ(umf_result, UMF_RESULT_ERROR_INVALID_ARGUMENT);
    ASSERT_EQ(os_memory_provider, nullptr);
}

// Default mode enabled when numa_list is set.
// For the default mode the nodeset must be empty.
TEST_F(testNuma, checkModeDefaultIllegalArgSet) {
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;

    std::vector<unsigned> numa_nodes = get_available_numa_nodes();

    os_memory_provider_params.numa_list = numa_nodes.data();
    os_memory_provider_params.numa_list_len = numa_nodes.size();

    umf_result_t umf_result;
    umf_result = umfMemoryProviderCreate(umfOsMemoryProviderOps(),
                                         &os_memory_provider_params,
                                         &os_memory_provider);
    ASSERT_EQ(umf_result, UMF_RESULT_ERROR_INVALID_ARGUMENT);
    ASSERT_EQ(os_memory_provider, nullptr);
}

// Bind mode enabled when numa_list is not set.
// For the bind mode the nodeset must be non-empty.
TEST_F(testNuma, checkModeBindIllegalArgSet) {
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_BIND;

    umf_result_t umf_result;
    umf_result = umfMemoryProviderCreate(umfOsMemoryProviderOps(),
                                         &os_memory_provider_params,
                                         &os_memory_provider);
    ASSERT_EQ(umf_result, UMF_RESULT_ERROR_INVALID_ARGUMENT);
    ASSERT_EQ(os_memory_provider, nullptr);
}

// Interleave mode enabled numa_list is not set.
// For the interleave mode the nodeset must be non-empty.
TEST_F(testNuma, checkModeInterleaveIllegalArgSet) {
    umf_os_memory_provider_params_t os_memory_provider_params =
        UMF_OS_MEMORY_PROVIDER_PARAMS_TEST;
    os_memory_provider_params.numa_mode = UMF_NUMA_MODE_INTERLEAVE;

    umf_result_t umf_result;
    umf_result = umfMemoryProviderCreate(umfOsMemoryProviderOps(),
                                         &os_memory_provider_params,
                                         &os_memory_provider);
    ASSERT_EQ(umf_result, UMF_RESULT_ERROR_INVALID_ARGUMENT);
    ASSERT_EQ(os_memory_provider, nullptr);
}
