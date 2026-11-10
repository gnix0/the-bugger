# 10 — Locals and Watches

[Guide index](README.md) · Previous: [Threads and Call Stack](09-threads_and_call_stack.md) · Next: [Memory](11-memory.md)

**Locals** discovers variable names belonging to the selected thread/frame. **Watches** keeps expressions you choose, such as a global counter or a structure member. A watch is useful when the same expression matters at several stops or when it is not a local in the currently displayed function.

Names, types and values have different availability. Seeing a name does not mean its value has been read. The current frontend requires deliberate value reads rather than automatically evaluating every displayed expression.

**On this page**

- [Read a local](#read-a-local)
- [Add and remove a watch](#add-and-remove-a-watch)
- [Expand a structure, array or pointer](#expand-a-structure-array-or-pointer)
- [Inspect a C++ object](#inspect-a-c-object)
- [Change numeric presentation](#change-numeric-presentation)
- [Example: watch a global and inspect a caller's locals](#example-watch-a-global-and-inspect-a-callers-locals)
- [Edit a supported scalar](#edit-a-supported-scalar)
- [If a value is missing](#if-a-value-is-missing)

## Read a local

1. Stop in the function you want to inspect. Use [Call Stack](09-threads_and_call_stack.md) if you need a caller's variables.
2. Expand the lower-middle group and select **Locals**.
3. Select a row by click or focused-body Up/Down.
4. Choose **Read**. Wait for the resulting sample or explanation.

**Read required** or `?` is a prompt to make that explicit request. `!` means unavailable, which may be caused by optimization, scope, unsupported evaluation or a backend error. It is not a numeric value. `*` and changed-value styling identify a changed comparable sample; frame/thread changes reset the comparison context.

After stepping, continuing or changing context, old values are stale or require another Read. Repeatedly opening the pane is not a refresh operation. **Actions → Refresh local names (no value read)** updates discovery without evaluating every variable.

## Add and remove a watch

Choose **Watches → + Watch**, or **Actions → Add watch expression**. Enter a supported expression and **Add**. Select the row and **Read** to obtain its value. Cancel/Escape abandons the input.

Supported forms include names, members, namespace qualification, constant indices and a simple numeric offset. Examples from the fixtures are:

```text
variables_global
variables_sample.count
variables_sample.values[3]
pointer->count
```

The last expression requires the appropriate frame and a valid pointer. Function calls such as `variables_ready(0)`, assignments and user casts are not accepted in this field. A name can be accepted when you add it but still fail to resolve when you Read it in the current frame.

Select a watch and use **Actions → Remove selected watch** to remove its root and tree. Watch definitions can be saved in a setup, but restored expressions are unread and grant no target-access permission.

## Expand a structure, array or pointer

Use a row's disclosure control or **Actions → Expand / collapse selected row (up to 32 children)**. Expansion is deliberate inspection; pointer expansion can read pointed-to storage. A pointer value alone does not prove the pointed-to address is ordinary memory. Avoid speculative peripheral expansion.

Use **Previous child page**/**Next child page** to inspect larger arrays or structures in pages of up to 32 children. The inspector supports up to 64 root entries, 256 loaded tree entries and 12 levels of expansion. Reaching a display limit does not mean the target object ends there; read the reported limit before interpreting a partial tree.

Read on a selected root discards its previous child page. Re-expand/repage after a fresh root read instead of assuming old children describe the new stop. Some C++ access groups require their own explicit expansion. Null, optimized-out or unsupported pointers/locations report their limitations rather than producing invented children. Specialized STL presentation is not provided.

## Inspect a C++ object

Ordinary C++ class/struct instances use the same **Read** and **Expand / collapse selected row** controls. If a `public`, `protected` or `private` group appears, expand it to reach its members. You can also add a simple member path such as `object.field` or `pointer->count` as a Watch in the frame where it is valid. Simple namespace-qualified names fit the expression syntax, but GDB must still resolve the name; acceptance in the field does not guarantee a value.

Try the existing C++ version of the variables example:

```sh
make build/variables-fixture-cpp
```

Launch `./build/variables-fixture-cpp` through **Linux application**. In Console, send `break variables_ready`, then `run`, as separate commands. At the first stop:

1. Select the `variables_checkpoint` caller in Call Stack, normally frame 1.
2. In Locals, select `object`, Read it, then expand it and its `public` group if shown. Its `field` member is 23; `nested` contains the copied Sample object.
3. Add `object.field` to Watches and Read; expect 23. Add `pointer->count` and Read; expect 17.
4. After stepping or changing frames, check the frame label and Read again before treating the result as current.

These fields are not a general C++ expression evaluator. Method/function calls, assignments, casts, template arguments and operators outside the listed member/index/simple-offset forms are rejected. Supported-looking member or index syntax still depends on GDB's ability to describe the storage; it does not promise overloaded-operator or smart-pointer behavior. There is no specialized STL-container browser, and displaying an object does not make the whole object editable. Use the separate [integer/bool editing restrictions](14-editing_values.md#edit-an-integer-or-boolean) for a supported scalar.

## Change numeric presentation

Select a row and choose **Actions → Cycle selected value format** to move among Natural, Hex, Decimal, Octal and Binary. The header/footer identifies the selected format. When a new formatted sample is required, the row can say **format read required; value is natural**. Read deliberately rather than interpreting a retained Natural value as already converted.

Formatting changes representation, not the program's value. Use [editing](14-editing_values.md) for supported mutations; choosing Hex is not a write.

## Example: watch a global and inspect a caller's locals

Build:

```sh
make build/variables-fixture
```

Launch `./build/variables-fixture`. In Console, send these separately:

```text
break variables_ready
run
```

1. At the first stop, add `variables_global` to Watches. It should require Read; after Read, expect 17.
2. Add/read `variables_sample`, expand it and inspect `count`, `text` and the `values` array. `count` begins at 17 and `text` contains `the-bugger`.
3. In Call Stack select the caller `variables_checkpoint`, normally frame 1 at this breakpoint. Locals now includes initialized `scalar`, `aggregate`, `pointer`, `null_pointer` and `array` when GDB can describe them.
4. Explicitly Read `scalar`; expect 17. Expand `aggregate`, then its array page. Expand `pointer` deliberately to inspect the object it points to; `null_pointer` has no valid object to inspect.
5. Return to the stopped frame and Continue to the second `variables_ready` stop. Read `variables_global` again; it is now 20. The caller's `scalar` is also 20 and its copied aggregate count has advanced.

You can use a [hardware watchpoint](08-breakpoints_and_watchpoints.md#create-a-hardware-data-watchpoint) to stop at the change itself rather than only at checkpoints.

## Edit a supported scalar

After an explicit fresh Read, **Actions → Edit selected integer/bool** opens the supported built-in integer/bool editor. Most aggregates, typedef forms, references and other unsupported types are not general-purpose editable expressions. The complete [Draft → Review → Apply → explicit verification workflow](14-editing_values.md) explains bounds, writable-region requirements and uncertain results.

## If a value is missing

First verify the thread/frame and whether the line initializing the variable has executed. Then Read at a fresh stop. An optimized build may have no recoverable value; a different frame may have no variable with that name. Refreshing names cannot recreate removed storage. Treat a stale Watch as a historical observation until a new Read succeeds.
