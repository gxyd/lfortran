program entry1
    real :: dummy
    dummy = 10
    call x(dummy)
    call y(dummy)
end program

subroutine x(dummy)
    print *, "Printed using subroutine call: ", dummy
    dummy = 5
    return
    entry y(dummy)
    print *, "Printed using entry statement: ", dummy
    return
end subroutine
