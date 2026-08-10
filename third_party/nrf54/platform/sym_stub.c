/*
 * Variant C: MPSL license interlock symbol (normally in libmpsl.a).
 *
 * Must be linked in the final executable, not in a static archive — the SL
 * binary is the only sym_* user and static link order would drop sym_stub.o
 * from an archive scan.
 */

void sym_AAFBZUDBSN44RWPA7VLGXWDL5UU6IQAP2VTRXLI(int license)
{
    (void)license;
}
