program inquire_01

    implicit none
    call check_inquire_01()

contains

    subroutine check_inquire_01()
        logical :: file_exists
        inquire (file='/this/will/fail.always', exist=file_exists)
        print *, file_exists
    end subroutine check_inquire_01


end program inquire_01
