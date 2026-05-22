"""Mirror of test/jacl/bench/spawn_chain.jacl.

50 sequential spawn+await pairs. asyncio.create_task is the closest
Python analog to JACL's spawn — both submit a unit of work to a
scheduler and return a handle the caller can await. Caveat: asyncio
runs on a single thread and the scheduler is cooperative, so this
measures task-creation + scheduling overhead but not cross-worker
handoff (which JACL's NxM runtime does pay).
"""

import asyncio


async def _unit(i: int) -> int:
    return i + 1


async def _main() -> int:
    acc = 0
    for i in range(50):
        f = asyncio.create_task(_unit(i))
        acc += await f
    return acc


def run() -> int:
    return asyncio.run(_main())


if __name__ == "__main__":
    print(run())
