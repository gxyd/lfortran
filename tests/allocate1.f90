program hello
implicit none
    character(:), allocatable :: cmd
    integer :: cmdlen
    integer :: ierr
    call get_command(length=cmdlen)

    if (cmdlen>0) then
        allocate(character(cmdlen) :: cmd)
        call get_command(cmd)
        print *, "hello ", cmd
    end if
    deallocate(cmd, stat = ierr)
end program
