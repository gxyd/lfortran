program array6
implicit none
contains
    subroutine t(n, a, b, c, d, e)
    integer, intent(in) :: n
    real :: a(*), b(3:*)
    real, dimension(*) :: c
    real, dimension(4:*) :: d
    real :: e(n:*)
    end subroutine
end program
