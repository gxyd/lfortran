SUBROUTINE DVOUT()
   CHARACTER*80       LINE
9999 FORMAT( / 1X, A, / 1X, A )

   WRITE( LOUT, FMT = 9999 )IFMT, LINE( 1: LLL )
END SUBROUTINE

PROGRAM MAIN
    CALL DVOUT()
END PROGRAM


