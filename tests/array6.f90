program array6
implicit none
contains
    subroutine t(n, a, b, c, d, e)
    integer, intent(in) :: n
    real :: a(*), b(3:*)
    real, dimension(3,n,*) :: c
    real, dimension(3,n,4:*) :: d
    real :: e(n:*)
    end subroutine
end program
